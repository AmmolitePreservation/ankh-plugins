"""The few Win32 calls the client needs, through ctypes."""
import ctypes
import subprocess
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

windowMessageClose = 0x0010
windowMessageGetText = 0x000D
windowMessageMouseMove = 0x0200
windowLongStyle = -16
systemMetricScreenWidth = 0
systemMetricScreenHeight = 1
showMinimizedWithoutActivating = 7
sendMessageAbortIfHung = 0x0002
controlTextMilliseconds = 2000
controlTextCapacity = 4096
dialogClass = "#32770"
staticClass = "Static"
snapshotProcesses = 0x00000002
invalidHandle = wintypes.HANDLE(-1).value
enumWindowsProcedure = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [enumWindowsProcedure, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsIconic.argtypes = [wintypes.HWND]
user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetForegroundWindow.restype = wintypes.HWND
user32.EnumChildWindows.argtypes = [wintypes.HWND, enumWindowsProcedure, wintypes.LPARAM]
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT,
  wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE


class PROCESSENTRY32W(ctypes.Structure):
  _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
    ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", ctypes.c_long), ("dwFlags", wintypes.DWORD),
    ("szExeFile", ctypes.c_wchar * 260)]


def raiseLastError(call):
  raise ctypes.WinError(ctypes.get_last_error(), f"{call} failed")


def processIDsNamed(executableName):
  snapshot = kernel32.CreateToolhelp32Snapshot(snapshotProcesses, 0)
  if snapshot == invalidHandle:
    raiseLastError("CreateToolhelp32Snapshot")
  try:
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    found = []
    hasEntry = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
    while hasEntry:
      if entry.szExeFile.lower() == executableName.lower():
        found.append(entry.th32ProcessID)
      hasEntry = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    return found
  finally:
    kernel32.CloseHandle(snapshot)


def topLevelWindows(processID):
  """The handles of the process's top-level windows, hidden ones included."""
  found = []

  def visit(window, _):
    owner = wintypes.DWORD()
    user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
    if owner.value == processID:
      found.append(window)
    return True

  user32.EnumWindows(enumWindowsProcedure(visit), 0)
  return found


def windowTitle(window):
  title = ctypes.create_unicode_buffer(256)
  user32.GetWindowTextW(window, title, len(title))
  return title.value


def windowClass(window):
  name = ctypes.create_unicode_buffer(256)
  if not user32.GetClassNameW(window, name, len(name)):
    raiseLastError("GetClassNameW")
  return name.value


def controlText(control):
  """The text of a control, which GetWindowText does not read in another process."""
  text = ctypes.create_unicode_buffer(controlTextCapacity)
  copied = ctypes.c_size_t()
  if not user32.SendMessageTimeoutW(control, windowMessageGetText, controlTextCapacity,
      ctypes.cast(text, ctypes.c_void_p).value, sendMessageAbortIfHung, controlTextMilliseconds, ctypes.byref(copied)):
    raiseLastError("SendMessageTimeoutW")
  return text.value


def messageBoxes(processID):
  """The message boxes the process shows, as (title, message)."""
  boxes = []
  for window in topLevelWindows(processID):
    if windowClass(window) != dialogClass or not user32.IsWindowVisible(window):
      continue
    texts = []

    def visit(control, _):
      if windowClass(control) == staticClass:
        text = controlText(control)
        if text:
          texts.append(text)
      return True

    user32.EnumChildWindows(window, enumWindowsProcedure(visit), 0)
    boxes.append((windowTitle(window), "\n".join(texts)))
  return boxes


def windowRectangle(window):
  """(left, top, right, bottom) on the screen."""
  rectangle = wintypes.RECT()
  if not user32.GetWindowRect(window, ctypes.byref(rectangle)):
    raiseLastError("GetWindowRect")
  return (rectangle.left, rectangle.top, rectangle.right, rectangle.bottom)


def visibleWindows(processID):
  """The process's visible top-level windows as (handle, title, (left, top, right, bottom))."""
  return [(window, windowTitle(window), windowRectangle(window)) for window in topLevelWindows(processID)
    if user32.IsWindowVisible(window)]


def windowStyle(window):
  return user32.GetWindowLongW(window, windowLongStyle) & 0xFFFFFFFF


def isVisible(window):
  return bool(user32.IsWindowVisible(window))


def isMinimized(window):
  return bool(user32.IsIconic(window))


def showWindow(window, command):
  user32.ShowWindow(window, command)


def postMouseMove(window, x, y):
  """Posts a mouse move at a client position, as the system does when the cursor moves over the window."""
  if not user32.PostMessageW(window, windowMessageMouseMove, 0, (y & 0xFFFF) << 16 | (x & 0xFFFF)):
    raiseLastError("PostMessageW")


def primaryMonitorSize():
  return (user32.GetSystemMetrics(systemMetricScreenWidth), user32.GetSystemMetrics(systemMetricScreenHeight))


def foregroundProcessID():
  owner = wintypes.DWORD()
  user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(owner))
  return owner.value


def postClose(window):
  if not user32.PostMessageW(window, windowMessageClose, 0, 0):
    raiseLastError("PostMessageW")



processQueryLimitedInformation = 0x1000
processTerminate = 0x0001
synchronize = 0x00100000
stillActive = 259
waitTimeout = 0x00000102
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]


def openProcess(processID):
  handle = kernel32.OpenProcess(processQueryLimitedInformation | processTerminate | synchronize, False, processID)
  if not handle:
    raiseLastError(f"OpenProcess({processID})")
  return handle


def exitCode(processHandle):
  """None while the process runs."""
  code = wintypes.DWORD()
  if not kernel32.GetExitCodeProcess(processHandle, ctypes.byref(code)):
    raiseLastError("GetExitCodeProcess")
  return None if code.value == stillActive else code.value


waitObject0 = 0x00000000


def waitForExit(processHandle, timeoutSeconds):
  """True once the process has ended, False when the wait timed out."""
  result = kernel32.WaitForSingleObject(processHandle, int(timeoutSeconds * 1000))
  if result == waitObject0:
    return True
  if result == waitTimeout:
    return False
  raiseLastError("WaitForSingleObject")


def terminateProcess(processHandle):
  if not kernel32.TerminateProcess(processHandle, 1):
    raiseLastError("TerminateProcess")


kernel32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
fileTimeTicksAtUnixEpoch = 116444736000000000


def processCreationTime(processHandle):
  """Seconds since the Unix epoch, as a float."""
  times = [wintypes.FILETIME() for _ in range(4)]
  if not kernel32.GetProcessTimes(processHandle, *[ctypes.byref(time) for time in times]):
    raiseLastError("GetProcessTimes")
  ticks = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
  return (ticks - fileTimeTicksAtUnixEpoch) / 10_000_000


def processImagePath(processHandle):
  size = wintypes.DWORD(32768)
  buffer = ctypes.create_unicode_buffer(size.value)
  if not kernel32.QueryFullProcessImageNameW(processHandle, 0, buffer, ctypes.byref(size)):
    raiseLastError("QueryFullProcessImageNameW")
  return buffer.value


def closeHandle(handle):
  if not kernel32.CloseHandle(handle):
    raiseLastError("CloseHandle")


startUseShowWindow = 0x00000001
showWithoutActivating = 4


class STARTUPINFOW(ctypes.Structure):
  _fields_ = [("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR), ("lpDesktop", wintypes.LPWSTR),
    ("lpTitle", wintypes.LPWSTR), ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD), ("dwXSize", wintypes.DWORD),
    ("dwYSize", wintypes.DWORD), ("dwXCountChars", wintypes.DWORD), ("dwYCountChars", wintypes.DWORD),
    ("dwFillAttribute", wintypes.DWORD), ("dwFlags", wintypes.DWORD), ("wShowWindow", wintypes.WORD),
    ("cbReserved2", wintypes.WORD), ("lpReserved2", ctypes.c_void_p), ("hStdInput", wintypes.HANDLE),
    ("hStdOutput", wintypes.HANDLE), ("hStdError", wintypes.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
  _fields_ = [("hProcess", wintypes.HANDLE), ("hThread", wintypes.HANDLE), ("dwProcessId", wintypes.DWORD),
    ("dwThreadId", wintypes.DWORD)]


kernel32.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL,
  wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(STARTUPINFOW), ctypes.POINTER(PROCESS_INFORMATION)]


def startProcess(executablePath, arguments, workingFolder, isActivated=False):
  """Starts the program, without activating its window unless isActivated (then as Explorer starts a program);
  returns its process handle, which the caller closes, and its ID."""
  if isActivated:
    startup = STARTUPINFOW(cb=ctypes.sizeof(STARTUPINFOW))
  else:
    startup = STARTUPINFOW(cb=ctypes.sizeof(STARTUPINFOW), dwFlags=startUseShowWindow, wShowWindow=showWithoutActivating)
  information = PROCESS_INFORMATION()
  commandLine = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(executablePath), *arguments]))
  if not kernel32.CreateProcessW(str(executablePath), commandLine, None, None, False, 0, None, str(workingFolder),
      ctypes.byref(startup), ctypes.byref(information)):
    raiseLastError(f"CreateProcessW({executablePath})")
  closeHandle(information.hThread)
  return information.hProcess, information.dwProcessId
