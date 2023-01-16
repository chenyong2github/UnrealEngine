// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#ifdef USE_ZLIB
#ifdef WITH_EDITOR

#define USE_UE
#ifdef USE_UE
#include "Containers/Map.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#else
#include <algorithm>
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <memory>
#include <stdexcept>
#include <stdexcept>
#include <string>
#endif

#include <istream>
#include <streambuf>

THIRD_PARTY_INCLUDES_START
#include "zlib.h"
#include <zconf.h>

THIRD_PARTY_INCLUDES_END
class IFileHandle;


struct ZIP_FILE_HEADER
{
	unsigned short version;
	unsigned short flags;
	unsigned short compression_type;
	unsigned short stamp_date, stamp_time;
	unsigned int crc;
	unsigned int compressed_size, uncompressed_size;
#ifdef USE_UE
	FString filename;
#else
	std::string filename;
#endif
	unsigned int header_offset; // local header offset

	ZIP_FILE_HEADER()
	{}

#ifdef USE_UE
	ZIP_FILE_HEADER(const FString& filename_input)
#else
	ZIP_FILE_HEADER(const std::string& filename_input)
#endif
		:version(20), flags(0), compression_type(8), stamp_date(0), stamp_time(0), crc(0),
		compressed_size(0), uncompressed_size(0), filename(filename_input), header_offset(0)
	{}

#ifdef USE_UE
	bool Read(IFileHandle* istream, const bool global, FString& err_msg)
#else
	bool Read(std::istream& istream, const bool global, std::string& err_msg)
#endif
	{
		unsigned int sig;
		// read and check for local/global magic
		if (global) {
			Read_Primitive(istream, sig);
			if (sig != 0x02014b50) 
			{ 
				//std::cerr << "Did not find global header signature" << std::endl; 
				err_msg += "ZIP_FILE_HEADER: Did not find global header signature.\n";
				return false; 
			}
			Read_Primitive(istream, version);
		}
		else 
		{
			Read_Primitive(istream, sig);
			if (sig != 0x04034b50) 
			{ 
				//LOG::cerr << "Did not find local header signature" << std::endl; 
				err_msg += "ZIP_FILE_HEADER: Did not find local header signature.\n";
				return false; 
			}
		}
		// Read rest of header
		Read_Primitive(istream, version);
		Read_Primitive(istream, flags);
		Read_Primitive(istream, compression_type);
		Read_Primitive(istream, stamp_date);
		Read_Primitive(istream, stamp_time);
		Read_Primitive(istream, crc);
		Read_Primitive(istream, compressed_size);
		Read_Primitive(istream, uncompressed_size);
		unsigned short filename_length, extra_length;
		Read_Primitive(istream, filename_length);
		Read_Primitive(istream, extra_length);
		unsigned short comment_length = 0;
		if (global) {
			Read_Primitive(istream, comment_length); // filecomment
			unsigned short disk_number_start, int_file_attrib;
			unsigned int ext_file_attrib;
			Read_Primitive(istream, disk_number_start); // disk# start
			Read_Primitive(istream, int_file_attrib); // internal file
			Read_Primitive(istream, ext_file_attrib); // ext final
			Read_Primitive(istream, header_offset);
		} // rel offset
		char* buf = new char[std::max(comment_length, std::max(filename_length, extra_length))];
#ifdef USE_UE
		istream->Read(reinterpret_cast<uint8*>(buf), filename_length);
#else
		istream.read(buf, filename_length);
#endif
		buf[filename_length] = 0;
#ifdef USE_UE
		filename = FString(buf);
		istream->Read(reinterpret_cast<uint8*>(buf), extra_length);
		if (global) istream->Read(reinterpret_cast<uint8*>(buf), comment_length);
#else
		filename = std::string(buf);
		istream.read(buf, extra_length);
		if (global) istream.read(buf, comment_length);
#endif
		delete[] buf;
		return true;
	}

protected:
#ifdef USE_UE
	template<class T>
	void
	Read_Primitive(IFileHandle* input, T& d)
	{
		//STATIC_ASSERT((sizeof(T) == PLATFORM_INDEPENDENT_SIZE<T>::value));
		input->Read((uint8*)&d, sizeof(T));
		//if (big_endian) Swap_Endianity(d); // convert to little endian if necessary
	}
#else
	template<class T>
	void
	Read_Primitive(std::istream& input, T& d)
	{
		//STATIC_ASSERT((sizeof(T) == PLATFORM_INDEPENDENT_SIZE<T>::value));
		input.read((char*)&d, sizeof(T));
		//if (big_endian) Swap_Endianity(d); // convert to little endian if necessary
	}
#endif
};

struct GZIP_FILE_HEADER
{
	unsigned char magic0, magic1; // magic should be 0x8b,0x1f
	unsigned char cm; // compression method 0x8 is gzip
	unsigned char flags; // flags
	unsigned int modtime; // 4 byte modification time
	unsigned char flags2; // secondary flags
	unsigned char os; // operating system 0xff for unknown
	unsigned short crc16; // crc check
	unsigned int crc32;

	GZIP_FILE_HEADER()
		:magic0(0), magic1(0), flags(0), modtime(0), flags2(0), os(0), crc16(0), crc32(0)
	{}

#ifdef USE_UE
	bool Read(IFileHandle* istream, FString& err_msg)
#else
	bool Read(std::istream& istream, std::string& err_msg)
#endif
	{
		Read_Primitive(istream, magic0);
		Read_Primitive(istream, magic1);
		if (magic0 != 0x1f || magic1 != 0x8b) 
		{ 
			//LOG::cerr << "gzip: did not find gzip magic 0x1f 0x8b" << std::endl;
			err_msg += "gzip: did not find gzip magic 0x1f 0x8b";
			return false; 
		}
		Read_Primitive(istream, cm);
		if (cm != 8) 
		{ 
			//LOG::cerr << "gzip: compression method not 0x8" << std::endl;
			err_msg += "gzip: compression method not 0x8";
			return false; 
		}
		Read_Primitive(istream, flags);
		Read_Primitive(istream, modtime);
		Read_Primitive(istream, flags2);
		Read_Primitive(istream, os);
		unsigned char dummyByte;
		// read flags if necessary
		if (flags & 2) {
			unsigned short flgExtraLen;
			Read_Primitive(istream, flgExtraLen);
			for (int k = 0; k < flgExtraLen; k++) 
				Read_Primitive(istream, dummyByte);
		}
		// read filename/comment if present
		int stringsToRead = ((flags & 8) ? 1 : 0) + ((flags & 4) ? 1 : 0);
		for (int i = 0; i < stringsToRead; i++)
			do { Read_Primitive(istream, dummyByte); } while (dummyByte != 0 && istream);
		if (flags & 1) Read_Primitive(istream, crc16);
#ifdef USE_UE
		if(!istream || istream->Tell() >= istream->Size())
#else
		if (!istream) 
#endif
		{
			//LOG::cerr << "gzip: got to end of file after only reading gzip header" << std::endl;
			err_msg += "gzip: got to end of file after only reading gzip header";
			return false; 
		}
		return true;
	}
protected:
#ifdef USE_UE
	template<class T>
	void
	Read_Primitive(IFileHandle* input, T& d)
	{
		//STATIC_ASSERT((sizeof(T) == PLATFORM_INDEPENDENT_SIZE<T>::value));
		input->Read((uint8*)&d, sizeof(T));
		//if (big_endian) Swap_Endianity(d); // convert to little endian if necessary
	}
#else
	template<class T>
	void
	Read_Primitive(std::istream& input, T& d)
	{
		//STATIC_ASSERT((sizeof(T) == PLATFORM_INDEPENDENT_SIZE<T>::value));
		input.read((char*)&d, sizeof(T));
		//if (big_endian) Swap_Endianity(d); // convert to little endian if necessary
}
#endif
};

class ZIP_STREAMBUF_DECOMPRESS : public std::streambuf
{
	static const unsigned int buffer_size = 512;
#ifdef USE_UE
	IFileHandle* istream;
#else
	std::istream& istream;
#endif

	z_stream strm;
	unsigned char in[buffer_size], out[buffer_size];
	ZIP_FILE_HEADER header;
	GZIP_FILE_HEADER gzip_header;
	int total_read, total_uncompressed;
	bool part_of_zip_file;
	bool valid;
	bool compressed_data;

	static const unsigned short DEFLATE = 8;
	static const unsigned short UNCOMPRESSED = 0;
public:
#ifdef USE_UE
	ZIP_STREAMBUF_DECOMPRESS(IFileHandle* stream, bool part_of_zip_file_input)
#else
	ZIP_STREAMBUF_DECOMPRESS(std::istream& stream, bool part_of_zip_file_input)
#endif
		:istream(stream), total_read(0), total_uncompressed(0), part_of_zip_file(part_of_zip_file_input), valid(true)
	{
		strm.zalloc = Z_NULL; strm.zfree = Z_NULL; strm.opaque = Z_NULL; strm.avail_in = 0; strm.next_in = Z_NULL;
		setg((char*)in, (char*)in, (char*)in);
		setp(0, 0);
		// skip the header
#ifdef USE_UE
		FString err_msg;
#else
		std::string err_msg;
#endif
		if (part_of_zip_file) {
			valid = header.Read(istream, false, err_msg);
			if (header.compression_type == DEFLATE) compressed_data = true;
			else if (header.compression_type == UNCOMPRESSED) compressed_data = false;
			else {
				compressed_data = false; 
#ifdef USE_UE
#else
				std::cerr << "ZIP: got unrecognized compressed data (Supported deflate/uncompressed)" << std::endl;
#endif
				valid = false;
			}
		}
		else { 
			valid = gzip_header.Read(istream, err_msg); 
			compressed_data = true; 
		}
		// initialize the inflate
		if (compressed_data && valid) {
			int result = inflateInit2(&strm, -MAX_WBITS);
			if (result != Z_OK) 
			{ 
#ifdef USE_UE
#else
				std::cerr << "gzip: inflateInit2 did not return Z_OK" << std::endl;
#endif
				valid = false; 
			}
		}
	}

	virtual ~ZIP_STREAMBUF_DECOMPRESS()
	{
		if (compressed_data && valid) inflateEnd(&strm);
//		if (!part_of_zip_file) delete& istream;
	}

	int process()
	{
		if (!valid) return -1;
		if (compressed_data) {
			strm.avail_out = buffer_size - 4;
			strm.next_out = (Bytef*)(out + 4);
			while (strm.avail_out != 0) {
				if (strm.avail_in == 0) { // buffer empty, read some more from file
#ifdef USE_UE
					int count = part_of_zip_file ? FGenericPlatformMath::Min((unsigned int)buffer_size, header.compressed_size - total_read) : (unsigned int)buffer_size;
					istream->Read(reinterpret_cast<uint8*>(in), count);
					strm.avail_in = count;
#else
					istream.read((char*)in, part_of_zip_file ? std::min((unsigned int)buffer_size, header.compressed_size - total_read) : (unsigned int)buffer_size);
					strm.avail_in = (uInt)istream.gcount();
#endif
					total_read += strm.avail_in;
					strm.next_in = (Bytef*)in;
				}
				int ret = inflate(&strm, Z_NO_FLUSH); // decompress via zlib
				switch (ret) {
				case Z_STREAM_ERROR:
#ifdef USE_UE
#else
					std::cerr << "libz error Z_STREAM_ERROR" << std::endl;
#endif
					valid = false; return -1;
				case Z_NEED_DICT:
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
#ifdef USE_UE
#else
					std::cerr << "gzip error " << strm.msg << std::endl;
#endif
					valid = false; return -1;
				}
				if (ret == Z_STREAM_END) break;
			}
			int unzip_count = buffer_size - strm.avail_out - 4;
			total_uncompressed += unzip_count;
			return unzip_count;
		}
		else { // uncompressed, so just read
#ifdef USE_UE
			int count = std::min(buffer_size - 4, header.uncompressed_size - total_read);
			istream->Read(reinterpret_cast<uint8*>(out + 4), count);
#else
			istream.read((char*)(out + 4), std::min(buffer_size - 4, header.uncompressed_size - total_read));
			int count = (int)istream.gcount();
#endif
			total_read += count;
			return count;
		}
		return 1;
	}

	virtual int underflow()
	{
		if (gptr() && (gptr() < egptr())) return traits_type::to_int_type(*gptr()); // if we already have data just use it
		int put_back_count = (int)(gptr() - eback());
		if (put_back_count > 4) put_back_count = 4;
		std::memmove(out + (4 - put_back_count), gptr() - put_back_count, put_back_count);
		int num = process();
		setg((char*)(out + 4 - put_back_count), (char*)(out + 4), (char*)(out + 4 + num));
		if (num <= 0) return EOF;
		return traits_type::to_int_type(*gptr());
	}

	virtual int overflow(int c = EOF)
	{
#ifdef USE_UE
#else
		assert(false);
#endif
		return EOF;
	}
};

// Class needed because istream cannot own its streambuf
class ZIP_FILE_ISTREAM : public std::istream
{
	ZIP_STREAMBUF_DECOMPRESS buf;
public:
#ifdef USE_UE
	ZIP_FILE_ISTREAM(IFileHandle* istream, bool part_of_zip_file)
		: std::istream(&buf)
		, buf(istream, part_of_zip_file)
	{}
#else
	ZIP_FILE_ISTREAM(std::istream& istream, bool part_of_zip_file)
		: std::istream(&buf)
		, buf(istream, part_of_zip_file)
	{}
#endif

	virtual ~ZIP_FILE_ISTREAM()
	{}
};

class ZIP_FILE_READER
{
#ifdef USE_UE
	IFileHandle* istream;
#else
	std::ifstream istream;
#endif
public:
#ifdef USE_UE
	TMap<FString, std::shared_ptr<ZIP_FILE_HEADER>> filename_to_header;
#else
	std::unordered_map<std::string, std::shared_ptr<ZIP_FILE_HEADER>> filename_to_header;
#endif

#ifdef USE_UE
	ZIP_FILE_READER(const FString& filename)
		: istream(nullptr)
#else
	ZIP_FILE_READER(const std::string& filename)
#endif
	{
#ifdef USE_UE
		FPlatformFileManager& FileManager = FPlatformFileManager::Get();
		IPlatformFile& PlatformFile = FileManager.GetPlatformFile();
		istream = PlatformFile.OpenRead(*filename, false);
#else
		istream.open(filename.c_str(), std::ios::in | std::ios::binary);
#endif
		if (!istream)
		{
	#if PLATFORM_EXCEPTIONS_DISABLED
			return;
	#else
			throw std::runtime_error("ZIP: Invalid file handle");
	#endif
		}

#ifdef USE_UE
		FString err_msg;
#else
		std::string err_msg;
#endif
		Find_And_Read_Central_Header(err_msg);
	}

	virtual ~ZIP_FILE_READER()
	{}

#ifdef USE_UE
	std::istream* Get_File(const FString& filename, const bool binary = true)
	{
		if (std::shared_ptr<ZIP_FILE_HEADER>* header = filename_to_header.Find(filename))
		{
			istream->Seek((*header)->header_offset);
			return new ZIP_FILE_ISTREAM(istream, true);
		}
		return nullptr;
	}
#else
	std::istream* Get_File(const std::string& filename, const bool binary = true)
	{
		//ZIP_FILE_HEADER** header = filename_to_header.Get_Pointer(filename);
		//auto it=filename_to_header.find(filename);
		auto it=filename_to_header.Find(FString(filename.c_str()));
		//if(it != filename_to_header.end())
		if(it != nullptr)
		{
			//ZIP_FILE_HEADER** header = &filename_to_header[filename].get();
			//if(ZIP_FILE_HEADER* header = it->second.get())
			if(ZIP_FILE_HEADER* header = it->get())
			{ 
				istream.seekg(header->header_offset); 
				return new ZIP_FILE_ISTREAM(istream, true); 
			}
		}
		return nullptr;
	}
#endif

#ifdef USE_UE
	void Get_File_List(TArray<FString>& filenames) const
	{
		filename_to_header.GetKeys(filenames);
	}
#else
	void Get_File_List(std::vector<std::string>& filenames) const
	{
		filenames.reserve(filename_to_header.size());
		for(const auto& kv : filename_to_header)
			filenames.push_back(kv.first);
	}
#endif

private:

#ifdef USE_UE
	template<class T>
	void
	Read_Primitive(IFileHandle* input, T& d)
	{
		//STATIC_ASSERT((sizeof(T) == PLATFORM_INDEPENDENT_SIZE<T>::value));
		input->Read(reinterpret_cast<uint8*>(&d), sizeof(T));
		//if (big_endian) Swap_Endianity(d); // convert to little endian if necessary
	}
#else
	template<class T>
	void
	Read_Primitive(std::istream& input, T& d)
	{
		//STATIC_ASSERT((sizeof(T) == PLATFORM_INDEPENDENT_SIZE<T>::value));
		input.read((char*)&d, sizeof(T));
		//if (big_endian) Swap_Endianity(d); // convert to little endian if necessary
	}
#endif

#ifdef USE_UE
	bool
	Find_And_Read_Central_Header(FString& err_msg)
	{
#else
	bool
	Find_And_Read_Central_Header(std::string& err_msg)
	{
#endif
		// Find the header
		// NOTE: this assumes the zip file header is the last thing written to file...
#ifdef USE_UE
		int end_position = istream->Size();
#else
		istream.seekg(0, std::ios_base::end);
		int end_position = istream.tellg();
#endif
		unsigned int max_comment_size = 0xffff; // max size of header
		unsigned int read_size_before_comment = 22;
		unsigned int read_start = max_comment_size + read_size_before_comment;
		if (static_cast<int>(read_start) > end_position) read_start = end_position;
#ifdef USE_UE
		istream->Seek(end_position - read_start);
#else
		istream.seekg(end_position - read_start);
#endif
		if (read_start <= 0) 
		{ 
			//LOG::cerr << "ZIP: Invalid read buffer size" << std::endl;
			err_msg += "ZIP_FILE_READER: Invalid read buffer size.\n";
			return false; 
		}
		char* buf = new char[(unsigned int)read_start];
#ifdef USE_UE
		istream->Read(reinterpret_cast<uint8*>(buf), read_start);
#else
		istream.read(buf, read_start);
#endif
		int found = -1;
		for (unsigned int i = 0; i < read_start - 3; i++) {
			if (buf[i] == 0x50 && buf[i + 1] == 0x4b && buf[i + 2] == 0x05 && buf[i + 3] == 0x06) 
			{ 
				found = i; 
				break; 
			}
		}
		delete[] buf;
		if (found == -1) 
		{ 
			//LOG::cerr << "ZIP: Failed to find zip header" << std::endl;
			err_msg += "ZIP_FILE_READER: Failed to find zip header.\n";
			return false; 
		}
		// seek to end of central header and read
#ifdef USE_UE
		istream->Seek(end_position - (read_start - found));
#else
		istream.seekg(end_position - (read_start - found));
#endif
		unsigned int word;
		unsigned short disk_number1, disk_number2, num_files, num_files_this_disk;
		Read_Primitive(istream, word); // end of central
		Read_Primitive(istream, disk_number1); // this disk number
		Read_Primitive(istream, disk_number2); // this disk number
		if (disk_number1 != disk_number2 || disk_number1 != 0) {
			//LOG::cerr << "ZIP: multiple disk zip files are not supported" << std::endl;
			err_msg += "ZIP_FILE_READER: multiple disk zip files are not supported.\n";
			return false;
		}
		Read_Primitive(istream, num_files); // one entry in center in this disk
		Read_Primitive(istream, num_files_this_disk); // one entry in center 
		if (num_files != num_files_this_disk) {
			//LOG::cerr << "ZIP: multi disk zip files are not supported" << std::endl;
			err_msg += "ZIP: multi disk zip files are not supported.\n";
			return false;
		}
		unsigned int size_of_header, header_offset;
		Read_Primitive(istream, size_of_header); // size of header
		Read_Primitive(istream, header_offset); // offset to header
		// go to header and read all file headers
#ifdef USE_UE
		istream->Seek(header_offset);
#else
		istream.seekg(header_offset);
#endif
		for (int i = 0; i < num_files; i++) 
		{
			std::shared_ptr<ZIP_FILE_HEADER> header(new ZIP_FILE_HEADER);
			if (header->Read(istream, true, err_msg))
			{
#ifdef USE_UE
				filename_to_header.Add(header->filename, header);
#else
				filename_to_header.insert(std::make_pair(header->filename, header));
#endif
			}
		}
		return true;
	}

};

#endif // WITH_EDITOR
#endif // USE_ZLIB
