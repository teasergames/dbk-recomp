"""Keep Windows setup workers tied to the headless installer's lifetime."""
import os

_job = None


def keep_children_in_job():
    """A launcher may terminate us without sending a Python signal.

    Windows closes this non-inherited job handle when we exit, including TerminateProcess.
    All descendants then stop before another setup can use the same destination. The handle
    deliberately stays open for this process's lifetime, including a normal return from setup.
    """
    global _job
    if os.name != 'nt' or _job is not None:
        return
    import ctypes as c
    from ctypes import wintypes as w

    class BasicLimits(c.Structure):
        _fields_ = [('process_time', c.c_int64), ('job_time', c.c_int64), ('flags', w.DWORD),
                    ('min_working_set', c.c_size_t), ('max_working_set', c.c_size_t),
                    ('active_processes', w.DWORD), ('affinity', c.c_size_t),
                    ('priority', w.DWORD), ('scheduling', w.DWORD)]

    class IoCounters(c.Structure):
        _fields_ = [(name, c.c_uint64) for name in ('read_ops', 'write_ops', 'other_ops',
                                                  'read_bytes', 'write_bytes', 'other_bytes')]

    class ExtendedLimits(c.Structure):
        _fields_ = [('basic', BasicLimits), ('io', IoCounters), ('process_memory', c.c_size_t),
                    ('job_memory', c.c_size_t), ('peak_process_memory', c.c_size_t),
                    ('peak_job_memory', c.c_size_t)]

    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [c.c_void_p, w.LPCWSTR]
    kernel.CreateJobObjectW.restype = w.HANDLE
    kernel.SetInformationJobObject.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
    kernel.SetInformationJobObject.restype = w.BOOL
    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
    kernel.AssignProcessToJobObject.restype = w.BOOL
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.CloseHandle.restype = w.BOOL
    handle = kernel.CreateJobObjectW(None, None)
    if not handle:
        raise c.WinError(c.get_last_error())
    limits = ExtendedLimits()
    limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not kernel.SetInformationJobObject(handle, 9, c.byref(limits), c.sizeof(limits)) or \
            not kernel.AssignProcessToJobObject(handle, kernel.GetCurrentProcess()):
        error = c.get_last_error()
        kernel.CloseHandle(handle)
        raise c.WinError(error)
    _job = handle
