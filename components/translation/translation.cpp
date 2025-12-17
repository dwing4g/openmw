#include "translation.hpp"

#include <fstream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <stdio.h>

#include <components/misc/pathhelpers.hpp>

namespace Translation
{
    Storage::Storage()
        : mEncoder(nullptr)
    {
    }

    void Storage::loadTranslationData(const Files::Collections& dataFileCollections, std::string_view esmFileName)
    {
        std::string_view esmNameNoExtension = Misc::stemFile(esmFileName);

        loadData(mCellNamesTranslations, esmNameNoExtension, "cel", dataFileCollections);
        loadData(mPhraseForms, esmNameNoExtension, "top", dataFileCollections);
        loadData(mTopicIDs, esmNameNoExtension, "mrk", dataFileCollections);
    }

    void Storage::loadData(ContainerType& container, std::string_view fileNameNoExtension, std::string_view extension,
        const Files::Collections& dataFileCollections)
    {
        std::string fileName(fileNameNoExtension);
        fileName += '.';
        fileName += extension;

        const Files::MultiDirCollection& collection = dataFileCollections.getCollection(extension);
        if (collection.doesExist(fileName))
        {
            std::ifstream stream(collection.getPath(fileName));

            if (!stream.is_open())
                throw std::runtime_error("failed to open translation file: " + fileName);

            loadDataFromStream(container, stream);
        }
    }

    void Storage::loadDataFromStream(ContainerType& container, std::istream& stream)
    {
        std::string line;
        while (!stream.eof() && !stream.fail())
        {
            std::getline(stream, line);
            if (!line.empty() && *line.rbegin() == '\r')
                line.resize(line.size() - 1);

            if (!line.empty())
            {
                const std::string_view utf8 = mEncoder->getUtf8(line);

                size_t tabPos = utf8.find('\t');
                if (tabPos != std::string::npos && tabPos > 0 && tabPos < utf8.size() - 1)
                {
                    const std::string_view key = utf8.substr(0, tabPos);
                    const std::string_view value = utf8.substr(tabPos + 1);

                    if (!key.empty() && !value.empty())
                        container.emplace(key, value);
                }
            }
        }
    }

    std::string_view Storage::translateCellName(std::string_view cellName) const
    {
        auto entry = mCellNamesTranslations.find(cellName);

        if (entry == mCellNamesTranslations.end())
            return cellName;

        return entry->second;
    }

    std::string_view Storage::topicID(std::string_view phrase) const
    {
        std::string_view result = topicStandardForm(phrase);

        // seeking for the topic ID
        auto topicIDIterator = mTopicIDs.find(result);

        if (topicIDIterator != mTopicIDs.end())
            result = topicIDIterator->second;

        return result;
    }

    std::string_view Storage::topicStandardForm(std::string_view phrase) const
    {
        auto phraseFormsIterator = mPhraseForms.find(phrase);

        if (phraseFormsIterator != mPhraseForms.end())
            return phraseFormsIterator->second;
        else
            return phrase;
    }

    void Storage::setEncoder(ToUTF8::Utf8Encoder* encoder)
    {
        mEncoder = encoder;
    }

    bool Storage::hasTranslation() const
    {
        return !mCellNamesTranslations.empty() || !mTopicIDs.empty() || !mPhraseForms.empty();
    }

    static unsigned int* pinyin = 0;
    static std::unordered_set<std::string> pinyinSet;
    static std::unordered_map<int, const char*> unicode2pinyinMap;

    static void init()
    {
        pinyin = (unsigned int*)calloc(0x7000, sizeof(unsigned int)); // [0x3000, 0xa000)
        FILE* const fp = fopen("pinyin.txt", "rb");
        if (fp)
        {
            std::unordered_map<unsigned int, unsigned int> map; // āáǎà ōóǒò ēéěè īíǐì ūúǔù üǖǘǚǜ ńňǹ m̄ḿm̀ ê̄ếê̌ề
            map.insert(std::make_pair(0xc481, 'a'));
            map.insert(std::make_pair(0xc3a1, 'a'));
            map.insert(std::make_pair(0xc78e, 'a'));
            map.insert(std::make_pair(0xc3a0, 'a'));
            map.insert(std::make_pair(0xc58d, 'o'));
            map.insert(std::make_pair(0xc3b3, 'o'));
            map.insert(std::make_pair(0xc792, 'o'));
            map.insert(std::make_pair(0xc3b2, 'o'));
            map.insert(std::make_pair(0xc493, 'e'));
            map.insert(std::make_pair(0xc3a9, 'e'));
            map.insert(std::make_pair(0xc49b, 'e'));
            map.insert(std::make_pair(0xc3a8, 'e'));
            map.insert(std::make_pair(0xc4ab, 'i'));
            map.insert(std::make_pair(0xc3ad, 'i'));
            map.insert(std::make_pair(0xc790, 'i'));
            map.insert(std::make_pair(0xc3ac, 'i'));
            map.insert(std::make_pair(0xc5ab, 'u'));
            map.insert(std::make_pair(0xc3ba, 'u'));
            map.insert(std::make_pair(0xc794, 'u'));
            map.insert(std::make_pair(0xc3b9, 'u'));
            map.insert(std::make_pair(0xc3bc, 'v'));
            map.insert(std::make_pair(0xc796, 'v'));
            map.insert(std::make_pair(0xc798, 'v'));
            map.insert(std::make_pair(0xc79a, 'v'));
            map.insert(std::make_pair(0xc79c, 'v'));
            map.insert(std::make_pair(0xc584, 'n'));
            map.insert(std::make_pair(0xc588, 'n'));
            map.insert(std::make_pair(0xc7b9, 'n'));
            map.insert(std::make_pair(0xcc80, 0));
            map.insert(std::make_pair(0xcc84, 0));
            map.insert(std::make_pair(0xcc8c, 0));
            map.insert(std::make_pair(0xe1b8bf, 'm'));
            map.insert(std::make_pair(0xc3aa, 'e'));
            map.insert(std::make_pair(0xe1babf, 'e'));
            map.insert(std::make_pair(0xe1bb81, 'e'));
            char pyBuf[8];
            // int n = 0;
            for (unsigned char buf[1024]; fgets((char*)buf, 1024, fp);)
            {
                // n++;
                if (*buf != 'U')
                    continue;
                unsigned int v = 0, i = 2;
                for (int c; (c = buf[i]) && c != ':'; i++)
                    v = (v << 4) + (c < 'A' ? c - '0' : c - 'A' + 10);
                int pyIdx = 0;
                for (bool f = true;;)
                {
                    int c = buf[++i];
                    if (!c || c == '#')
                        break;
                    if (c == ' ' || c == ',')
                    {
                        if (pyIdx > 0)
                        {
                            pyBuf[pyIdx] = 0;
                            unicode2pinyinMap.insert(std::make_pair(v, pinyinSet.insert(std::string(pyBuf)).first->c_str()));
                            pyIdx = -1;
                        }
                        f = true;
                    }
                    else
                    {
                        if (c >= 'a' && c <= 'z')
                        {
                            if (f)
                            {
                                if (v >= 0x3000 && v < 0xa000)
                                    pinyin[v - 0x3000] |= 1U << (c - 'a');
                                f = false;
                            }
                            if (pyIdx >= 0)
                                pyBuf[pyIdx++] = (char)c;
                        }
                        else if (c < 0xe0)
                        {
                            const auto it = map.find((c << 8) + buf[++i]);
                            if (it != map.end())
                            {
                                c = it->second;
                                if (c)
                                {
                                    if (f)
                                    {
                                        if (v >= 0x3000 && v < 0xa000)
                                            pinyin[v - 0x3000] |= 1U << (c - 'a');
                                        f = false;
                                    }
                                    if (pyIdx >= 0)
                                        pyBuf[pyIdx++] = (char)c;
                                }
                            }
                            // else
                            //     printf("pinyin = %d\n", n);
                        }
                        else
                        {
                            const auto it = map.find((c << 16) + (buf[i + 1] << 8) + buf[i + 2]);
                            i += 2;
                            if (it != map.end())
                            {
                                c = it->second;
                                if (f)
                                {
                                    if (v >= 0x3000 && v < 0xa000)
                                        pinyin[v - 0x3000] |= 1U << (c - 'a');
                                    f = false;
                                }
                                if (pyIdx >= 0)
                                    pyBuf[pyIdx++] = (char)c;
                            }
                            // else
                            //     printf("pinyin = %d\n", n);
                        }
                    }
                }
            }
            fclose(fp);
        }
    }

    bool isFirstChar(const unsigned int first, const char checkChar)
    {
        if (!pinyin)
            init();

        if (first >= 0x3000 && first < 0xa000)
        {
            const unsigned int v = pinyin[first - 0x3000];
            if (!((v >> (checkChar - 'a')) & 1) && (v || checkChar != 'v'))
                return false;
        }
        else if (first != (unsigned char)checkChar && (first >= 'a' && first <= 'z' || checkChar != 'v'))
            return false;
        return true;
    }
    /*
    void translateCellName(std::string& str)
    {
        static std::unordered_map<std::string, std::string>* cellname = 0;
        if (!cellname)
        {
            cellname = new std::unordered_map<std::string, std::string>;
            FILE* fp = fopen("cellname.txt", "rb");
            if (fp)
            {
                std::string src;
                for (char buf[1024]; fgets(buf, 1024, fp);)
                {
                    size_t n = strlen(buf);
                    while (n > 0 && ((unsigned char*)buf)[n - 1] <= 0x20)
                        n--;
                    buf[n] = 0;
                    if (*buf == '>')
                        src.assign(buf + 1, n - 1);
                    else if (*buf == '=' && !src.empty())
                    {
                        (*cellname)[src] = std::string(buf + 1, n - 1);
                        src.clear();
                    }
                }
                fclose(fp);
            }
        }

        auto it = cellname->find(str);
        if (it != cellname->end())
            str = it->second;
    }
    */

    static int parseUnicode(const std::string_view s, size_t& i)
    {
        int c = (uint8_t)s[i];
        if (c < 0x80)
        {
            i++;
            return c;
        }
        const int h = c & 0xf0;
        if (h == 0xe0)
        {
            c = ((c & 0xf) << 12) + ((s[i + 1] & 0x3f) << 6) + (s[i + 2] & 0x3f);
            i += 3;
            return c;
        }
        if (h == 0xf0)
        {
            c = ((c & 7) << 18) + ((s[i + 1] & 0x3f) << 12) + ((s[i + 2] & 0x3f) << 6) + (s[i + 3] & 0x3f);
            i += 4;
            return c;
        }
        c = ((c & 0x1f) << 6) + (s[i + 1] & 0x3f);
        i += 2;
        return c;
    }

    static const char* unicode2pinyin(const int unicode)
    {
        if (unicode < 0x80)
            return "";
        if (!pinyin)
            init();
        const auto it = unicode2pinyinMap.find(unicode);
        return it != unicode2pinyinMap.end() ? it->second : "";
    }

    int compareStrByPinyin(const std::string_view a, const std::string_view b) // must be valid utf-8
    {
        const size_t an = a.size();
        const size_t bn = b.size();
        for (size_t ai = 0, bi = 0;;)
        {
            if (ai >= an)
            {
                if (bi >= bn)
                    return 0;
                return -1;
            }
            else if (bi >= bn)
                return 1;
            int ac = parseUnicode(a, ai);
            int bc = parseUnicode(b, bi);
            if (ac == bc)
                continue;
            if ((ac | bc) < 0x80)
            {
                if (ac >= 'A' && ac <= 'Z')
                    ac += 0x20;
                if (bc >= 'A' && bc <= 'Z')
                    bc += 0x20;
                if (ac == bc)
                    continue;
                return ac - bc;
            }
            const char* const ap = unicode2pinyin(ac);
            const char* const bp = unicode2pinyin(bc);
            const int c = strcmp(ap, bp);
            return c != 0 ? c : ac - bc;
        }
    }
}
