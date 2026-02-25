#ifndef GAME_MWDIALOGUE_KEYWORDSEARCH_H
#define GAME_MWDIALOGUE_KEYWORDSEARCH_H

#include <algorithm>
#include <set>
#include <map>
#include <stdexcept>
#include <vector>

#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>

namespace MWDialogue
{

    template <typename Value>
    class KeywordSearch
    {
    public:
        using Point = std::string::const_iterator;

        struct Match
        {
            Point mBeg;
            Point mEnd;
            Value mValue;
        };

        void seed(std::string_view keyword, Value value)
        {
            if (keyword.empty())
                return;
            buildTrie(keyword, value, 0, mRoot);
        }

        void clear()
        {
            mRoot.mChildren.clear();
            mRoot.mKeyword.clear();
        }

        bool containsKeyword(std::string_view keyword, Value& value) const
        {
            const Entry* current = &mRoot;
            for (char c : keyword)
            {
                auto it = current->mChildren.find(Misc::StringUtils::toLower(c));
                if (it == current->mChildren.end())
                    return false;
                else if (Misc::StringUtils::ciEqual(it->second.mKeyword, keyword))
                {
                    value = it->second.mValue;
                    return true;
                }
                current = &it->second;
            }
            return false;
        }

        void highlightKeywords(Point beg, Point end, std::vector<Match>& out) const
        {
            std::vector<Match> matches;
            for (Point i = beg; i != end; ++i)
            {
                if (i != beg)
                {
                    Point prev = i;
                    --prev;
                    constexpr std::string_view wordSeparators = "\n\r \t'\"";
                    if (wordSeparators.find(*prev) == std::string_view::npos && (unsigned char)*i < 0xe0) // for any 3/4-bytes utf-8 char
                        continue;
                }

                const Entry* current = &mRoot;
                for (Point it = i; it != end; ++it)
                {
                    auto found = current->mChildren.find(Misc::StringUtils::toLower(*it));
                    if (found == current->mChildren.end())
                        break;
                    current = &found->second;
                    if (!current->mKeyword.empty())
                    {
                        std::string_view remainingText(it + 1, end);
                        std::string_view remainingKeyword = std::string_view(current->mKeyword).substr(it + 1 - i);
                        if (Misc::StringUtils::ciStartsWith(remainingText, remainingKeyword))
                        {
                            Match match;
                            match.mValue = current->mValue;
                            match.mBeg = i;
                            match.mEnd = i + current->mKeyword.size();
                            matches.push_back(match);
                        }
                    }
                }
            }
            // resolve overlapping keywords
            while (!matches.empty())
            {
                std::size_t longestKeywordSize = 0;
                auto longestKeyword = matches.begin();
                for (auto it = matches.begin(); it != matches.end(); ++it)
                {
                    std::size_t size = it->mEnd - it->mBeg;
                    if (size > longestKeywordSize)
                    {
                        longestKeywordSize = size;
                        longestKeyword = it;
                    }

                    auto next = it + 1;

                    if (next == matches.end())
                        break;

                    if (it->mEnd <= next->mBeg)
                    {
                        break; // no overlap
                    }
                }

                Match keyword = *longestKeyword;
                matches.erase(longestKeyword);
                out.push_back(keyword);
                // erase anything that overlaps with the keyword we just added to the output
                std::erase_if(matches,
                    [&](const Match& match) { return match.mBeg < keyword.mEnd && match.mEnd > keyword.mBeg; });
            }

            std::sort(
                out.begin(), out.end(), [](const Match& left, const Match& right) { return left.mBeg < right.mBeg; });
        }

        static bool removeUnusedPostfix(std::string& text, std::vector<Match>& matches)
        {
            if (text.empty() || text.back() != '}')
                return false;
            const auto pos = text.find_last_of('{');
            if (pos == std::string::npos)
                return false;
            auto it = text.cbegin() + pos;
            if (matches.empty() || matches.back().mEnd < it)
                text.erase(pos > 0 && text[pos - 1] == ' ' ? pos - 1 : pos);
            else
            {
                it++;
                size_t n = 0;
                std::set<std::string> kws;
                for (auto i = matches.begin(); i != matches.end();)
                {
                    auto& match = *i;
                    match.mBeg -= n;
                    match.mEnd -= n;
                    if (match.mEnd <= it)
                        kws.insert(std::string(match.mBeg, match.mEnd));
                    else
                    {
                        if (match.mBeg < it || match.mEnd >= text.cend()
                            || *(match.mBeg - 1) != ',' && *(match.mBeg - 1) != '{'
                            || *match.mEnd != ',' && *match.mEnd != '}'
                            || kws.contains(std::string(match.mBeg, match.mEnd)))
                        {
                            i = matches.erase(i);
                            continue;
                        }
                        if (it < match.mBeg)
                        {
                            auto s = match.mBeg - it;
                            n += s;
                            text.erase(it, match.mBeg);
                            match.mBeg -= s;
                            match.mEnd -= s;
                        }
                        it = match.mEnd;
                        if (it < text.cend() && *it == ',')
                            it++;
                    }
                    i++;
                }
                if (it - 1 == text.cbegin() + pos)
                    text.erase(pos > 0 && text[pos - 1] == ' ' ? pos - 1 : pos);
                else if (it < text.cend() - 1)
                    text.erase(*(it - 1) == ',' ? it - 1 : it, text.cend() - 1);
            }
            return true;
        }

    private:
        struct Entry
        {
            std::string mKeyword;
            Value mValue;
            std::map<char, Entry> mChildren;
        };

        void buildTrie(std::string_view keyword, Value value, size_t depth, Entry& entry)
        {
            const char ch = Misc::StringUtils::toLower(keyword[depth]);
            const auto found = entry.mChildren.find(ch);

            if (found == entry.mChildren.end())
            {
                entry.mChildren[ch].mValue = value;
                entry.mChildren[ch].mKeyword = keyword;
            }
            else
            {
                if (!found->second.mKeyword.empty())
                {
                    std::string_view existingKeyword = found->second.mKeyword;
                    if (Misc::StringUtils::ciEqual(keyword, existingKeyword))
                        throw std::runtime_error("duplicate keyword inserted");
                    if (depth >= existingKeyword.size())
                        throw std::runtime_error("unexpected");
                    // Turn this Entry into a branch and append a leaf to hold its current value
                    if (depth + 1 < existingKeyword.size())
                    {
                        buildTrie(existingKeyword, found->second.mValue, depth + 1, found->second);
                        found->second.mKeyword.clear();
                    }
                }
                if (depth + 1 == keyword.size())
                {
                    found->second.mValue = value;
                    found->second.mKeyword = keyword;
                }
                else // depth+1 < keyword.size()
                    buildTrie(keyword, value, depth + 1, found->second);
            }
        }

        Entry mRoot;
    };

}

#endif
