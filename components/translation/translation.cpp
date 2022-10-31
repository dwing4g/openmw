#include "translation.hpp"

#include <fstream>
#include <string>
#include <unordered_map>
#include <stdio.h>

namespace Translation
{
    Storage::Storage()
        : mEncoder(nullptr)
    {
    }

    void Storage::loadTranslationData(const Files::Collections& dataFileCollections, const std::string& esmFileName)
    {
        std::string esmNameNoExtension(Misc::StringUtils::lowerCase(esmFileName));
        // changing the extension
        size_t dotPos = esmNameNoExtension.rfind('.');
        if (dotPos != std::string::npos)
            esmNameNoExtension.resize(dotPos);

        loadData(mCellNamesTranslations, esmNameNoExtension, ".cel", dataFileCollections);
        loadData(mPhraseForms, esmNameNoExtension, ".top", dataFileCollections);
        loadData(mTopicIDs, esmNameNoExtension, ".mrk", dataFileCollections);
    }

    void Storage::loadData(ContainerType& container, const std::string& fileNameNoExtension,
        const std::string& extension, const Files::Collections& dataFileCollections)
    {
        std::string fileName = fileNameNoExtension + extension;

        if (dataFileCollections.getCollection(extension).doesExist(fileName))
        {
            std::ifstream stream(dataFileCollections.getCollection(extension).getPath(fileName).c_str());

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

                size_t tab_pos = utf8.find('\t');
                if (tab_pos != std::string::npos && tab_pos > 0 && tab_pos < utf8.size() - 1)
                {
                    const std::string_view key = utf8.substr(0, tab_pos);
                    const std::string_view value = utf8.substr(tab_pos + 1);

                    if (!key.empty() && !value.empty())
                        container.emplace(key, value);
                }
            }
        }
    }

    std::string Storage::translateCellName(const std::string& cellName) const
    {
        std::map<std::string, std::string>::const_iterator entry = mCellNamesTranslations.find(cellName);

        if (entry == mCellNamesTranslations.end())
            return cellName;

        return entry->second;
    }

    std::string Storage::topicID(const std::string& phrase) const
    {
        std::string result = topicStandardForm(phrase);

        // seeking for the topic ID
        std::map<std::string, std::string>::const_iterator topicIDIterator = mTopicIDs.find(result);

        if (topicIDIterator != mTopicIDs.end())
            result = topicIDIterator->second;

        return result;
    }

    std::string Storage::topicStandardForm(const std::string& phrase) const
    {
        std::map<std::string, std::string>::const_iterator phraseFormsIterator = mPhraseForms.find(phrase);

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

    bool isFirstChar(unsigned int first, char checkChar)
    {
        static unsigned int* pinyin = 0;
        if (!pinyin)
        {
            pinyin = (unsigned int*)calloc(0x7000, sizeof(unsigned int)); // [0x3000, 0xA000)
            FILE* fp = fopen("pinyin.txt", "rb");
            if (fp)
            {
                std::unordered_map<unsigned int, unsigned int> map; // āáǎà ōóǒò ēéěè
                map.insert(std::make_pair(0xc481, 'a'));
                map.insert(std::make_pair(0xc3a1, 'a'));
                map.insert(std::make_pair(0xc78e, 'a'));
                map.insert(std::make_pair(0xc3a0, 'a'));
                map.insert(std::make_pair(0xc58d, 'e'));
                map.insert(std::make_pair(0xc3b3, 'e'));
                map.insert(std::make_pair(0xc792, 'e'));
                map.insert(std::make_pair(0xc3b2, 'e'));
                map.insert(std::make_pair(0xc493, 'o'));
                map.insert(std::make_pair(0xc3a9, 'o'));
                map.insert(std::make_pair(0xc49b, 'o'));
                map.insert(std::make_pair(0xc3a8, 'o'));
                for (char buf[1024]; fgets(buf, 1024, fp);)
                {
                    if (*buf != 'U')
                        continue;
                    unsigned int v = 0, i = 2;
                    for (int c; (c = buf[i]) && c != ':'; i++)
                        v = (v << 4) + (c < 'A' ? c - '0' : c - 'A' + 10);
                    if (v < 0x3000 || v >= 0xA000)
                        continue;
                    for (bool f = true;;)
                    {
                        int c = buf[i++];
                        if (!c || c == '#')
                            break;
                        if (c == ' ' || c == ',')
                            f = true;
                        else if (f)
                        {
                            if (c >= 'a' && c <= 'z')
                            {
                                pinyin[v - 0x3000] |= 1U << (c - 'a');
                                f = false;
                            }
                            else
                            {
                                auto it = map.find(((unsigned char)c << 8) + (unsigned char)buf[i]);
                                if (it != map.end())
                                {
                                    pinyin[v - 0x3000] |= 1U << (it->second - 'a');
                                    f = false;
                                }
                            }
                        }
                    }
                }
                fclose(fp);
            }
        }

        if (first >= 0x3000 && first < 0xA000)
        {
            unsigned int v = pinyin[first - 0x3000];
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
}
