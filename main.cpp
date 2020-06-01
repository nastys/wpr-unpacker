/*
MIT License

Copyright (c) 2020 nastys

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <iostream>
#include <QtZlib/zlib.h>

char *reveal_filename(char *buffer, int len)
{
    char *result;

    char *buf2=buffer;
    for(int i=0; i<len; i++)
    {
        int a = 2 * ((*buf2 & 0x40) != 0) | 16 * ((*buf2 & 8) != 0) | 32 * ((*buf2 & 4) != 0) | (((*buf2 & 2) != 0) << 6) | (((*buf2 & 1) != 0) << 7) | 8 * ((*buf2 & 0x10) != 0) | 4 * ((*buf2 & 0x20) != 0);
        bool b = *buf2++ < 0;
        *(buf2 - 1) = a | b;
    }
    result = &buffer[len - 1];

    char *buf3=buffer;
    for (int i = 0; i<len/2; i++)
    {
        result[1] = *buf3++;
        *(buf3 - 1) = *result--;
    }

    if ( len > 0 )
    {
        result = buffer;
        for(int i=0; i<len; i++)
        {
            char t = i ^ *result;
            *result++ = t;
            *(result - 1) = t ^ 0x52;
        }
    }

    return result;
}

char *reveal_data(char *unc_data_buffer_end, char *unc_data_buffer_start, int len)
{
  unc_data_buffer_end = unc_data_buffer_start;

  for(int i=0; i<len; i++)
  {
      int a = 4 * ((*unc_data_buffer_end & 0x20) != 0) | 8 * ((*unc_data_buffer_end & 0x10) != 0) | 16
              * ((*unc_data_buffer_end & 8) != 0) | 32 * ((*unc_data_buffer_end & 4) != 0) | (((*unc_data_buffer_end & 2) != 0) << 6) | (((*unc_data_buffer_end & 1) != 0) << 7) | 2 * ((*unc_data_buffer_end & 0x40) != 0);
      bool b = *unc_data_buffer_end++ < 0;
      *(unc_data_buffer_end - 1) = b | a;
  }

  return unc_data_buffer_end;
}

bool gzDecompress(QByteArray qba_compressed, QByteArray &qba_decompressed)
{
    qba_decompressed.clear();
    if(qba_compressed.length() == 0) return 1;

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;

    int result = inflateInit2(&stream, 31);
    if (result != Z_OK) return 0;

    char *compressed_data = qba_compressed.data();
    int compressed_data_left = qba_compressed.length();

    do
    {
        int chunk_len = qMin(32768, compressed_data_left);

        if(chunk_len <= 0)
            break;

        stream.next_in = (unsigned char*)compressed_data;
        stream.avail_in = chunk_len;

        compressed_data += chunk_len;
        compressed_data_left -= chunk_len;

        do
        {
            char out[32768];

            stream.next_out = (unsigned char*)out;
            stream.avail_out = 32768;

            result = inflate(&stream, Z_NO_FLUSH);

            switch (result)
            {
            case Z_NEED_DICT:
                result = Z_DATA_ERROR;
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
            case Z_STREAM_ERROR:
                inflateEnd(&stream);
                return(false);
            }

            int out_len = (32768 - stream.avail_out);
            if(out_len > 0) qba_decompressed.append((char*)out, out_len);
        }
        while (stream.avail_out == 0);

    }
    while (result != Z_STREAM_END);

    inflateEnd(&stream);
    return (result == Z_STREAM_END);
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    std::cout << "Whopper (WPR) Unpacker 0.1 by nastys\n";
    if(a.arguments().count()==1)
    {
        std::cout << QString("Usage:\t"+QFileInfo(a.arguments().first()).fileName()+" <filename.wpr> [outfolder]\n\nAny existing files will be overwritten.\n").toStdString();
        return 0;
    }
    QString outdir;
    if(a.arguments().count()<3)
        outdir=a.arguments().at(1)+"-unpacked";
    else outdir=a.arguments().at(2);

    QFile wpr_in(a.arguments().at(1));
    if(!wpr_in.open(QIODevice::ReadOnly))
    {
        std::cout << "E: Could not open file!" << std::endl;
        return 1;
    }
    QByteArray wprtext = wpr_in.read(64);
    std::cout << "File magic: " << wprtext.toStdString() << std::endl;

    QStringList filelist;
    unsigned long gzstart=0;
    for(unsigned int i=0; true; i++)
    {
        QByteArray filename=wpr_in.read(32);
        char buffer[32];
        if((filename.at(0)==0x1F)&&(filename.at(1)==(char)0x8B))
        {
            gzstart=wpr_in.pos()-32;
            break;
        }
        for(unsigned int i=0; i<sizeof(buffer); i++)
            buffer[i]=filename.at(i);
        reveal_filename(buffer, sizeof(buffer));
        filelist << buffer;
    }
    wpr_in.reset();
    wpr_in.seek(gzstart);
    QByteArray obdata_compressed = wpr_in.readAll();
    wpr_in.close();
    unsigned long sizec=obdata_compressed.size();

    QByteArrayList compressed_files;
    QByteArray temp;
    for(unsigned long i=0; i<sizec; i++)
    {
        temp.append(obdata_compressed.at(i));
        if(i<sizec-10&&(obdata_compressed.at(i)==0x00&&obdata_compressed.at(i+1)==0x1f&&(unsigned char)obdata_compressed.at(i+2)==0x8b&&obdata_compressed.at(i+3)==0x08
                &&obdata_compressed.at(i+4)==0x00&&obdata_compressed.at(i+5)==0x00&&obdata_compressed.at(i+6)==0x00&&obdata_compressed.at(i+7)==0x00
                &&obdata_compressed.at(i+8)==0x00&&obdata_compressed.at(i+9)==0x00&&obdata_compressed.at(i+10)==0x00))
        {
            compressed_files.append(temp);
            temp.clear();
        }
    }
    compressed_files.append(temp);
    temp.append((char)0);
    temp.clear();

    QDir().mkdir(outdir);
    for(int i=0; i<compressed_files.size(); i++)
    {
        std::cout << QString("Inflating \""+filelist.at(i)+"\"...\n").toStdString();
        gzDecompress(compressed_files.at(i), temp);
        reveal_data(temp.data()+temp.size(), temp.data(), temp.size());
        QFile dump(outdir+"/"+filelist.at(i));
        dump.open(QIODevice::ReadWrite);
        dump.resize(0);
        dump.write(temp);
        dump.flush();
        dump.close();
        temp.clear();
    }
    return 0;
    return a.exec();
}
