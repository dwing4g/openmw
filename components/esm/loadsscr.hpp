#ifndef OPENMW_ESM_SSCR_H
#define OPENMW_ESM_SSCR_H

#include <string>

namespace ESM
{

class ESMReader;
class ESMWriter;

/*
 Startup script. I think this is simply a 'main' script that is run
 from the begining. The SSCR records contain a DATA identifier which
 is totally useless (TODO: don't remember what it contains exactly,
 document it below later.), and a NAME which is simply a script
 reference.
 */

struct StartScript
{
    static unsigned int sRecordId;
    /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
    static std::string getRecordType() { return "StartScript"; }

    std::string mData;
    std::string mId;

    bool mIsDeleted;

    StartScript();

    // Load a record and add it to the list
    void load(ESMReader &esm);
    void save(ESMWriter &esm) const;

    void blank();
};

}
#endif
