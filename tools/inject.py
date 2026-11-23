"""Development only (allowed by the user 2026-11-16): load MK9Access\\MK9Access.dll into the running mk9.exe
and call core_start. Stands in for the version.dll loader while Defender quarantines it.
Usage: python inject.py"""
import ctypes, ctypes.wintypes as w, os, sys, pefile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mk9

k = ctypes.windll.kernel32
k.OpenProcess.restype = w.HANDLE
k.VirtualAllocEx.restype = ctypes.c_void_p
k.VirtualAllocEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_size_t, w.DWORD, w.DWORD]
k.WriteProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
k.CreateRemoteThread.restype = w.HANDLE
k.CreateRemoteThread.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, w.DWORD, ctypes.c_void_p]
k.GetModuleHandleW.restype = ctypes.c_void_p
k.GetProcAddress.restype = ctypes.c_void_p
k.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
k.GetExitCodeThread.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
k.CreateToolhelp32Snapshot.restype = w.HANDLE


class ME(ctypes.Structure):
    _fields_ = [('dwSize', w.DWORD), ('th32ModuleID', w.DWORD), ('th32ProcessID', w.DWORD), ('GlblcntUsage', w.DWORD),
                ('ProccntUsage', w.DWORD), ('modBaseAddr', ctypes.c_void_p), ('modBaseSize', w.DWORD),
                ('hModule', w.HMODULE), ('szModule', ctypes.c_wchar * 256), ('szExePath', ctypes.c_wchar * 260)]


def module_base(pid, name):
    s = k.CreateToolhelp32Snapshot(0x18, pid)
    e = ME(); e.dwSize = ctypes.sizeof(ME)
    ok = k.Module32FirstW(s, ctypes.byref(e)); base = None
    while ok:
        if e.szModule.lower() == name.lower():
            base = e.modBaseAddr; break
        ok = k.Module32NextW(s, ctypes.byref(e))
    k.CloseHandle(s)
    return base


def remote_call(h, fn, arg):
    t = k.CreateRemoteThread(h, None, 0, ctypes.c_void_p(fn), ctypes.c_void_p(arg), 0, None)
    k.WaitForSingleObject(t, 15000)
    code = w.DWORD(); k.GetExitCodeThread(t, ctypes.byref(code)); k.CloseHandle(t)
    return code.value


def main():
    pid = mk9.find_pid()
    if not pid:
        print('game not running'); return 1
    if module_base(pid, 'MK9Access.dll'):
        print('already loaded'); return 0
    dll = os.path.join(mk9.GAME, 'MK9Access', 'MK9Access.dll')
    h = k.OpenProcess(0x1F0FFF, False, pid)
    buf = (dll + '\0').encode('utf-16-le')
    mem = k.VirtualAllocEx(h, None, len(buf), 0x3000, 0x04)
    k.WriteProcessMemory(h, mem, buf, len(buf), None)
    remote_call(h, k.GetProcAddress(k.GetModuleHandleW('kernel32.dll'), b'LoadLibraryW'), mem)
    base = module_base(pid, 'MK9Access.dll')
    if not base:
        print('LoadLibrary failed'); return 1
    pe = pefile.PE(dll, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    rva = next(s.address for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name == b'core_start')
    remote_call(h, base + rva, 0)
    print('injected, core_start called')
    return 0


if __name__ == '__main__':
    sys.exit(main())
