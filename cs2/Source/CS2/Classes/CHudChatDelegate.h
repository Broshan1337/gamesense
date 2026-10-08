#pragma once

namespace cs2
{











struct CHudChatDelegate;















using ChatPrint = void*(CHudChatDelegate* thisptr, unsigned int filter, const char* format, ...);

}
