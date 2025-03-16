
#include "content/common/common.h"
#include <windows.h>
#include <string>

namespace content {

void printFuncName(const char* name, bool needPrint, bool needBreak)
{
    if (needPrint) {
        std::string output("printFuncName:");
        output += name;
        output += "\n";
        OutputDebugStringA(output.c_str());
    }

    if (needBreak)
        DebugBreak();
}

}