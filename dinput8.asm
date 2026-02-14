.code
extern OrignalDirectInput8Create: QWORD
FakeDirectInput8Create PROC
	jmp  OrignalDirectInput8Create
FakeDirectInput8Create ENDP

extern OrignalDllCanUnloadNow: QWORD
FakeDllCanUnloadNow PROC
	jmp  OrignalDllCanUnloadNow
FakeDllCanUnloadNow ENDP

extern OrignalDllGetClassObject: QWORD
FakeDllGetClassObject PROC
	jmp  OrignalDllGetClassObject
FakeDllGetClassObject ENDP

extern OrignalDllRegisterServer: QWORD
FakeDllRegisterServer PROC
	jmp  OrignalDllRegisterServer
FakeDllRegisterServer ENDP

extern OrignalDllUnregisterServer: QWORD
FakeDllUnregisterServer PROC
	jmp  OrignalDllUnregisterServer
FakeDllUnregisterServer ENDP

extern OrignalGetdfDIJoystick: QWORD
FakeGetdfDIJoystick PROC
	jmp  OrignalGetdfDIJoystick
FakeGetdfDIJoystick ENDP
end