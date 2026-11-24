#include "WindowsIdentity.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

namespace WindowsIdentity {

QString appUserModelId()
{
    return QString::fromWCharArray(kAppUserModelId);
}

QString startMenuShortcutPath()
{
#ifdef Q_OS_WIN
    PWSTR programsPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programsPath)) && programsPath) {
        const QString dir = QString::fromWCharArray(programsPath);
        CoTaskMemFree(programsPath);
        return QDir(dir).filePath(QStringLiteral("Chiikawa.lnk"));
    }
#endif
    return {};
}

#ifdef Q_OS_WIN
namespace {

bool shortcutMatchesCurrentExe(const QString& lnkPath, const QString& exeNativePath)
{
    if (!QFileInfo::exists(lnkPath)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IShellLinkW> shellLink;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&shellLink)))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IPersistFile> persistFile;
    if (FAILED(shellLink.As(&persistFile))) {
        return false;
    }

    const std::wstring lnkWide = lnkPath.toStdWString();
    if (FAILED(persistFile->Load(lnkWide.c_str(), STGM_READ))) {
        return false;
    }

    wchar_t existingTarget[MAX_PATH] = {};
    if (FAILED(shellLink->GetPath(existingTarget, MAX_PATH, nullptr, SLGP_RAWPATH))) {
        return false;
    }
    const QString existingStr = QDir::toNativeSeparators(QString::fromWCharArray(existingTarget));
    if (existingStr.compare(exeNativePath, Qt::CaseInsensitive) != 0) {
        return false;
    }

    Microsoft::WRL::ComPtr<IPropertyStore> propStore;
    if (FAILED(shellLink.As(&propStore))) {
        return false;
    }

    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool idMatches = false;
    if (SUCCEEDED(propStore->GetValue(PKEY_AppUserModel_ID, &pv))) {
        if (pv.vt == VT_LPWSTR && pv.pwszVal != nullptr) {
            idMatches = (wcscmp(pv.pwszVal, kAppUserModelId) == 0);
        }
        PropVariantClear(&pv);
    }
    return idMatches;
}

void ensureStartMenuShortcut()
{
    const QString lnkPath = startMenuShortcutPath();
    if (lnkPath.isEmpty()) {
        return;
    }

    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (exePath.isEmpty()) {
        return;
    }

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(coHr);

    if (!shortcutMatchesCurrentExe(lnkPath, exePath)) {
        Microsoft::WRL::ComPtr<IShellLinkW> shellLink;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&shellLink)))) {
            const std::wstring exeWide = exePath.toStdWString();
            const std::wstring workDirWide =
                QDir::toNativeSeparators(QFileInfo(exePath).absolutePath()).toStdWString();

            shellLink->SetPath(exeWide.c_str());
            shellLink->SetWorkingDirectory(workDirWide.c_str());
            shellLink->SetDescription(L"Chiikawa Desktop Study Pet");
            shellLink->SetIconLocation(exeWide.c_str(), 0);

            Microsoft::WRL::ComPtr<IPropertyStore> propStore;
            if (SUCCEEDED(shellLink.As(&propStore))) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                pv.vt = VT_LPWSTR;
                const size_t bytes = (wcslen(kAppUserModelId) + 1) * sizeof(wchar_t);
                pv.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
                if (pv.pwszVal) {
                    memcpy(pv.pwszVal, kAppUserModelId, bytes);
                    propStore->SetValue(PKEY_AppUserModel_ID, pv);
                    propStore->Commit();
                    PropVariantClear(&pv);
                }
            }

            Microsoft::WRL::ComPtr<IPersistFile> persistFile;
            if (SUCCEEDED(shellLink.As(&persistFile))) {
                const QFileInfo lnkInfo(lnkPath);
                QDir().mkpath(lnkInfo.absolutePath());
                const std::wstring lnkWide = QDir::toNativeSeparators(lnkPath).toStdWString();
                persistFile->Save(lnkWide.c_str(), TRUE);
            }
        }
    }

    if (shouldUninit) {
        CoUninitialize();
    }
}

} // namespace
#endif

void initialize()
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
    ensureStartMenuShortcut();
#endif
}

} // namespace WindowsIdentity
