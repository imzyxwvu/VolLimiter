#include <Windows.h>
#include <mmdeviceapi.h> 
#include <endpointvolume.h>
#include <audioclient.h>
#include <stdexcept>

using namespace std;

#define EVENT_VOLBEYONDLIMIT 0x2033

class CAudioEndpointVolumeCallback : public IAudioEndpointVolumeCallback
{
    LONG _cRef;
	DWORD mThreadId;
public:
	float mLimit;
    explicit CAudioEndpointVolumeCallback(float limit) : _cRef(1), mLimit(limit) {
		mThreadId = GetThreadId(GetCurrentThread());
	}
    ~CAudioEndpointVolumeCallback() {}
    ULONG STDMETHODCALLTYPE AddRef()
    {
        return InterlockedIncrement(&_cRef);
    }
    ULONG STDMETHODCALLTYPE Release()
    {
        ULONG ulRef = InterlockedDecrement(&_cRef);
        if (0 == ulRef)
            delete this;
        return ulRef;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface)
    {
        if (IID_IUnknown == riid)
        {
            AddRef();
            *ppvInterface = (IUnknown*)this;
        }
        else if (__uuidof(IAudioEndpointVolumeCallback) == riid)
        {
            AddRef();
            *ppvInterface = (IAudioEndpointVolumeCallback*)this;
        }
        else
        {
            *ppvInterface = NULL;
            return E_NOINTERFACE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify)
    {
        if (pNotify == NULL)
            return E_INVALIDARG;
		if(pNotify->fMasterVolume > mLimit) {
			PostThreadMessage(mThreadId, WM_USER, EVENT_VOLBEYONDLIMIT, NULL);
		}
        return S_OK;
    }
};

class VolumeMonitor {
public:
	static IMMDeviceEnumerator* pDeviceEnumerator;
	explicit VolumeMonitor(IAudioEndpointVolumeCallback *cb);
	~VolumeMonitor() {
		pAudioEndpointVolume->Release();
		pDevice->Release();
	}
	IAudioEndpointVolume *operator->() const {
		return pAudioEndpointVolume;
	}
private:
	IMMDevice* pDevice;
	IAudioEndpointVolume* pAudioEndpointVolume;
	VolumeMonitor(const VolumeMonitor &) {};
};

IMMDeviceEnumerator *VolumeMonitor::pDeviceEnumerator = NULL;

VolumeMonitor::VolumeMonitor(IAudioEndpointVolumeCallback *cb) : pDevice(NULL), pAudioEndpointVolume(NULL) {
	HRESULT hr;
	if(!pDeviceEnumerator) {
		hr = CoCreateInstance(
			__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), (void**)&pDeviceEnumerator);
		if(FAILED(hr))
			throw runtime_error("CoCreateInstance");
	}
	hr = pDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
	if(FAILED(hr))
		throw runtime_error("GetDefaultAudioEndpoint");
	hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&pAudioEndpointVolume);
	if(FAILED(hr)) {
		pDevice->Release();
		throw runtime_error("Activate");
	}
	hr = pAudioEndpointVolume->RegisterControlChangeNotify(cb);
	if(FAILED(hr)) {
		pAudioEndpointVolume->Release();
		pDevice->Release();
		throw runtime_error("RegisterControlChangeNotify");
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nCmdShow)
{
	HANDLE hMutex = CreateMutex(NULL, false, L"VolumeLimiter");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
		MessageBox(NULL, L"Already running!", NULL, MB_OK | MB_ICONWARNING);
		return 0;
    }

	CoInitialize(0);
    CAudioEndpointVolumeCallback callback(0.6f);
	VolumeMonitor monitor(&callback);
	MSG msg;
	while(GetMessage(&msg, NULL, 0, 0)) {
		if(msg.message == WM_USER && msg.wParam == EVENT_VOLBEYONDLIMIT) {
			monitor->SetMasterVolumeLevelScalar(callback.mLimit, &GUID_NULL);
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}