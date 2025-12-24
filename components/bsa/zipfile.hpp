#ifndef OPENMW_COMPONENTS_BSA_ZIPFILE_HPP
#define OPENMW_COMPONENTS_BSA_ZIPFILE_HPP

#include <string.h>
#include <istream>
#include <mutex>

#include <minizip/unzip.h>

#include <components/files/conversion.hpp>
#include <components/files/istreamptr.hpp>
#include <components/vfs/archive.hpp>
#include <components/vfs/file.hpp>

namespace Bsa
{
    struct ZipBuffer : std::streambuf
    {
        ZipBuffer(char* buf, size_t size)
        {
            setg(buf, buf, buf + size);
        }
    };

    class ZipStream : ZipBuffer, public std::istream
    {
    public:
        ZipStream(char* buf, size_t size)
            : ZipBuffer(buf, size)
            , std::istream(this)
            , m_buf(buf)
        {
        }

        virtual ~ZipStream()
        {
            delete[] m_buf;
        }

    private:
        char* const m_buf;
    };

    class ZipArchive;

    class ZipFile : public VFS::File
    {
    public:
        ZipFile(ZipArchive* zip, const char* filename, const unz_file_info64& fi, const unz64_file_pos& fp)
            : m_zip(zip)
            , m_fp(fp)
            , m_size(fi.uncompressed_size)
            , m_filetime(dosToFileTime(fi.dosDate))
            , m_stem(filenameToStem(filename))
        {
        }

        static std::filesystem::file_time_type dosToFileTime(uLong dosTime)
        {
            int year = static_cast<int>(((dosTime >> 25) & 0x7f) + 1980);
            unsigned month = static_cast<unsigned>((dosTime >> 21) & 0xf);
            unsigned day = static_cast<unsigned>((dosTime >> 16) & 0x1f);
            int hour = static_cast<int>((dosTime >> 11) & 0x1f);
            int minute = static_cast<int>((dosTime >> 5) & 0x3f);
            int second = static_cast<int>((dosTime & 0x1f) * 2);
            auto st = std::chrono::sys_days{ std::chrono::year_month_day{
                std::chrono::year{ year }, std::chrono::month{ month }, std::chrono::day{ day } } } +
                std::chrono::hours{ hour } + std::chrono::minutes{ minute } + std::chrono::seconds{ second };
            return std::chrono::clock_cast<std::chrono::file_clock>(st);
        }

        static std::string filenameToStem(const char* filename)
        {
            size_t n = strlen(filename);
            const char* p = filename, *q = 0;
            for (size_t i = n; i-- > 0;)
            {
                char c = filename[i];
                if (c == '/' || c == '\\')
                {
                    p = filename + i + 1;
                    break;
                }
                else if (c == '.' && !q)
                    q = filename + i;
            }
            if (!q)
                q = filename + n;
            return std::string(p, q - p);
        }

        Files::IStreamPtr open() override;

        std::filesystem::path getPath() override { return std::filesystem::path();

        }

//        std::filesystem::file_time_type getLastModified() const override
//        {
//            return m_filetime;
//        }
//
//        std::string getStem() const override
//        {
//            return m_stem;
//        }

    private:
        ZipArchive* const m_zip; // 8 bytes
        const unz64_file_pos m_fp; // 16 bytes
        const ZPOS64_T m_size; // 8 bytes
        const std::filesystem::file_time_type m_filetime; // 8 bytes
        const std::string m_stem; // 32+n bytes
    };

    class ZipArchive : public VFS::Archive
    {
    public:
        ZipArchive(const std::filesystem::path& filename)
            : m_ufp(unzOpen2_64(&filename, &filefunc64))
            , m_desc("ZIP: " + Files::pathToUnicodeString(filename))
        {
            unz_file_info64 fi;
            unz64_file_pos fp;
            char buf[512];
            for (int r = unzGoToFirstFile(m_ufp); r == UNZ_OK; r = unzGoToNextFile(m_ufp))
            {
                r = unzGetCurrentFileInfo64(m_ufp, &fi, buf, sizeof(buf), 0, 0, 0, 0); // buf(filename) should be utf-8
                if (r == UNZ_OK)
                {
                    size_t n = strlen(buf);
                    if (n > 0 && buf[n - 1] != '/' && buf[n - 1] != '\\')
                    {
                        r = unzGetFilePos64(m_ufp, &fp);
                        if (r == UNZ_OK)
                            m_fileMap[VFS::Path::Normalized(buf)] = new ZipFile(this, buf, fi, fp);
                    }
                }
            }
        }

        virtual ~ZipArchive()
        {
            for (const auto& [_, pf] : m_fileMap)
                delete pf;
            unzClose(m_ufp);
        }

        void listResources(VFS::FileMap& out) override
        {
            for (const auto& [k, v] : m_fileMap)
                out[k] = v;
        }

        bool contains(VFS::Path::NormalizedView file) const override
        {
            return m_fileMap.find(file) != m_fileMap.end();
        }

        std::string getDescription() const override
        {
            return m_desc;
        }

        std::mutex& getMutex()
        {
            return m_mutex;
        }

        unzFile getUnzFile() const
        {
            return m_ufp;
        }

    private:
        static zlib_filefunc64_def filefunc64;

        const unzFile m_ufp;
        const std::string m_desc;
        std::mutex m_mutex;
        VFS::FileMap m_fileMap;
    };
}

#endif
