local win32 = require "win32"
local apis = win32.apis
local c = win32.constants

local function u2w(str)
    local wlen = apis.MultiByteToWideChar(c.CP_UTF8, 0, str, #str, nil, 0)
    local wstr = win32.memory((wlen+1)*2)
    apis.MultiByteToWideChar(c.CP_UTF8, 0, str, #str, wstr, wlen)
    return wstr
end

apis.MessageBoxW(0, u2w "你好!", u2w "Win32", c.MB_HELP | c.MB_ICONINFORMATION);
