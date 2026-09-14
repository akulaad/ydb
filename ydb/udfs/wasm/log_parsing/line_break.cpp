#include "line_break.h"

#include <util/string/split.h>

namespace NLogParsing {

bool SplitLineBreak(TStringBuf chunk, TVector<TStringBuf>* records) {
    records->clear();
    StringSplitter(chunk).SplitByString("\n").SkipEmpty().Collect(records);
    return !records->empty();
}

} // namespace NLogParsing
