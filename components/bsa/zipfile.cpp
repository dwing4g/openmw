#include "zipfile.hpp"

namespace Bsa
{
    Files::IStreamPtr ZipFile::open()
    {
        unzFile m_ufp = m_zip->getUnzFile();
        char* buf = new char[m_size];
        {
            std::lock_guard<std::mutex> guard(m_zip->getMutex());
            unzGoToFilePos64(m_ufp, &m_fp);
            if (unzOpenCurrentFile(m_ufp) != UNZ_OK)
            {
                delete[] buf;
                return nullptr;
            }
            unzReadCurrentFile(m_ufp, buf, static_cast<unsigned>(m_size)); //TODO: support 4G+ file?
            unzCloseCurrentFile(m_ufp);
        }
        return std::make_unique<ZipStream>(buf, static_cast<size_t>(m_size));
    }

    static voidpf ZCALLBACK fopen64_file_func(voidpf _, const void* filename, int mode)
    {
        if (!filename || (mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ)
            return 0;
        return new std::ifstream(*static_cast<const std::filesystem::path*>(filename), std::ios_base::binary);
    }

    static uLong ZCALLBACK fread_file_func(voidpf _, voidpf stream, void* buf, uLong size)
    {
        if (!stream || !buf || !size)
            return 0;
        std::ifstream& is = *static_cast<std::ifstream*>(stream);
        is.read(static_cast<char*>(buf), static_cast<std::streamsize>(size));
        return static_cast<uLong>(is.gcount());
    }

    static uLong ZCALLBACK fwrite_file_func(voidpf _, voidpf stream, const void* buf, uLong size)
    {
        return 0;
    }

    static ZPOS64_T ZCALLBACK ftell64_file_func(voidpf _, voidpf stream)
    {
        if (!stream)
            return static_cast<ZPOS64_T>(-1);
        std::ifstream& is = *static_cast<std::ifstream*>(stream);
        return static_cast<ZPOS64_T>(is.tellg());
    }

    static long ZCALLBACK fseek64_file_func(voidpf _, voidpf stream, ZPOS64_T offset, int origin)
    {
        if (!stream)
            return -1;
        std::ios_base::seekdir dir;
        switch (origin)
        {
            case ZLIB_FILEFUNC_SEEK_SET:
                dir = std::ios_base::beg;
                break;
            case ZLIB_FILEFUNC_SEEK_CUR:
                dir = std::ios_base::cur;
                break;
            case ZLIB_FILEFUNC_SEEK_END:
                dir = std::ios_base::end;
                break;
            default:
                return -1;
        }
        std::ifstream& is = *static_cast<std::ifstream*>(stream);
        return is.seekg(static_cast<std::streamoff>(offset), dir).fail() ? -1 : 0;
    }

    static int ZCALLBACK fclose_file_func(voidpf _, voidpf stream)
    {
        if (!stream)
            return -1;
        std::ifstream* is = static_cast<std::ifstream*>(stream);
        is->close();
        delete is;
        return 0;
    }

    static int ZCALLBACK ferror_file_func(voidpf _, voidpf stream)
    {
        if (!stream)
            return -1;
        return static_cast<std::ifstream*>(stream)->fail() ? 1 : 0;
    }

    zlib_filefunc64_def ZipArchive::filefunc64 =
    {
        fopen64_file_func,
        fread_file_func,
        fwrite_file_func,
        ftell64_file_func,
        fseek64_file_func,
        fclose_file_func,
        ferror_file_func,
        0
    };
}
