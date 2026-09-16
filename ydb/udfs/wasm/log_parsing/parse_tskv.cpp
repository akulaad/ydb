#include "parse_tskv.h"

#include <util/string/split.h>

namespace NLogParsing {

TTskvParseResult ParseTskv(TStringBuf raw) {
    TTskvParseResult result;
    TStringBuf body = raw;
    if (body.StartsWith("tskv\t")) {
        body.Skip(5);
    }

    TVector<TStringBuf> tokens;
    StringSplitter(body).Split('\t').SkipEmpty().Collect(&tokens);
    for (TStringBuf token : tokens) {
        const size_t eq = token.find('=');
        if (eq == TStringBuf::npos) {
            continue;
        }
        const TStringBuf key = token.Head(eq);
        const TStringBuf value = token.Tail(eq + 1);
        if (key.empty()) {
            continue;
        }
        result.Fields[TString(key)] = TString(value);
    }

    if (result.Fields.empty()) {
        result.Successed = false;
        result.Error = "cannot parse tskv";
        return result;
    }

    result.Successed = true;
    return result;
}

} // namespace NLogParsing
