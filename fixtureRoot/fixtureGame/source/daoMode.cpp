#include "daoMode.h"

#include "fixtureReport.h"

#include <windows.h>
#include <ocidl.h>

static const CLSID dao35DBEngine = { 0x00000010, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4 } };
// Every program built with DAO carries this key, which DAO's class factories ask for.
static const wchar_t daoLicenseKey[] = L"mbmabptebkjcdlgtjmskjwtsdhjbmkmwtrak";

static const int reportUnavailable = 8;
static const int stepFailed = 9;

static IDispatch* createEngine() {
  IClassFactory2* factory = nullptr;
  const HRESULT classResult = CoGetClassObject(dao35DBEngine, CLSCTX_INPROC_SERVER, nullptr, IID_IClassFactory2,
    reinterpret_cast<void**>(&factory));
  writeReport("daoClassObject result=0x%08lX", static_cast<unsigned long>(classResult));
  if (FAILED(classResult)) {
    failStep("classObject", static_cast<unsigned long>(classResult));
    return nullptr;
  }
  BSTR key = SysAllocString(daoLicenseKey);
  IDispatch* engine = nullptr;
  const HRESULT createResult = factory->CreateInstanceLic(nullptr, nullptr, IID_IDispatch, key,
    reinterpret_cast<void**>(&engine));
  SysFreeString(key);
  factory->Release();
  if (FAILED(createResult)) {
    failStep("createInstance", static_cast<unsigned long>(createResult));
    return nullptr;
  }
  return engine;
}

static bool reportVersion(IDispatch* engine) {
  OLECHAR* propertyName = const_cast<OLECHAR*>(L"Version");
  DISPID propertyID = 0;
  const HRESULT nameResult = engine->GetIDsOfNames(IID_NULL, &propertyName, 1, LOCALE_USER_DEFAULT, &propertyID);
  if (FAILED(nameResult)) {
    return failStep("versionName", static_cast<unsigned long>(nameResult));
  }
  DISPPARAMS noArguments = {};
  VARIANT version;
  VariantInit(&version);
  const HRESULT readResult = engine->Invoke(propertyID, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &noArguments,
    &version, nullptr, nullptr);
  if (FAILED(readResult) || version.vt != VT_BSTR) {
    VariantClear(&version);
    return failStep("versionRead", static_cast<unsigned long>(readResult));
  }
  writeReport("daoEngine version=%ls", version.bstrVal);
  VariantClear(&version);
  return true;
}

int runDAOMode() {
  if (!openReport()) {
    return reportUnavailable;
  }
  const HRESULT initializeResult = CoInitialize(nullptr);
  if (FAILED(initializeResult)) {
    failStep("coInitialize", static_cast<unsigned long>(initializeResult));
    return stepFailed;
  }
  IDispatch* const engine = createEngine();
  if (engine == nullptr) {
    return stepFailed;
  }
  const bool isReported = reportVersion(engine);
  engine->Release();
  if (!isReported) {
    return stepFailed;
  }
  writeReport("ready");
  return 0;
}
