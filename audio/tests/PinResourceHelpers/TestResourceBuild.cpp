// ------------------------------------------------------------------------------
//
// Copyright (C) Microsoft Corporation. All rights reserved.
//
// File Name:
//
//  TestResourceBuild.cpp
//
// Abstract:
//
//  TAEF BuildResourceList implementation
//
// ------------------------------------------------------------------------------
#include "PreComp.h"

#include <Functiondiscoverykeys_devpkey.h>
#include <PropertyHelper.h>
#include <TestResource.h>

using namespace WEX::Common;
using namespace WEX::Logging;
using namespace WEX::TestExecution;

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CreateTestResource
//
// Create the test resource with MMDevice, device id, device name, data flow, connector type, connector id, mode,
// list of formats
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT CreateTestResource
(
    ResourceList&           resourceList,
    DeviceDescriptor        descriptor
)
{
    HRESULT                             hr = S_OK;
    CComHeapPtr<CHalfApp>               spHalfApp;
    wil::com_ptr_nothrow<ITestResource> spTestResource;
    GUID                                ResourceGUID;
    CComBSTR                            szResourceName;

    // Create HalfApp
    spHalfApp.Attach(new CHalfApp(descriptor));
    if (!VERIFY_IS_NOT_NULL(spHalfApp)) {
        hr = E_OUTOFMEMORY;
        return hr;
    }

    // Create PinTestResource
    if (!VERIFY_SUCCEEDED(hr = CoCreateGuid(&ResourceGUID))) {
        return hr;
    }
    if (!VERIFY_SUCCEEDED(hr = CPinTestResource::CreateInstance(
        spHalfApp, ResourceGUID, &spTestResource))) {
        return hr;
    }

    // Add to resource list
    if (!VERIFY_SUCCEEDED(hr = spTestResource->GetValue(CComBSTR(TestResourceProperty::c_szName), &szResourceName))) {
        return hr;
    }
    spHalfApp.Detach();
    if (!VERIFY_SUCCEEDED(hr = resourceList.Add(spTestResource.get()))) {
        return hr;
    }
    Log::Comment(String().Format(L"Test Resource (%s) added", (PCWSTR)szResourceName));
    
    return hr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// GetProcessingModesForConnector
//
// For host/keyword detector pin, processing mode info is cached in property store and can be directly read. For offload
// pin, query for ks processing mode property. For loopback pin, it doesn't support any processing modes, return GUID_NULL.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT GetProcessingModesForConnector
(
    IMMDevice*              pDevice,
    UINT                    uConnectorId,
    EndpointConnectorType   eConnectorType,
    ULONG                   *pCount,
    AUDIO_SIGNALPROCESSINGMODE **ppModes
)
{
    HRESULT hr = S_OK;

    *pCount = 0;
    *ppModes = NULL;

    if (eConnectorType == eHostProcessConnector || eConnectorType == eKeywordDetectorConnector) {
        if (!VERIFY_SUCCEEDED(hr = GetCachedProcessingModes(pDevice, eConnectorType, pCount, ppModes))) {
            return hr;
        }
    }
    else if (eConnectorType == eOffloadConnector) {
        if (!VERIFY_SUCCEEDED(hr = GetProcessingModes(pDevice, uConnectorId, pCount, ppModes))) {
            return hr;
        }
    }
    else if (eConnectorType == eLoopbackConnector) {
        // Loopback pin doesn't support any processing modes, so put 1 GUID_NULL
        CComHeapPtr<AUDIO_SIGNALPROCESSINGMODE>     spModes;

        if (!spModes.Allocate(1))
            return E_OUTOFMEMORY;

        spModes[0] = GUID_NULL;

        *pCount = 1;
        *ppModes = spModes.Detach();
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// GetDefaultFormatForConnector
//
// Read audio engine device format from property store as default format. 
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT GetDefaultFormatForConnector
(
    IMMDevice*              pDevice,
    EndpointConnectorType   eConnectorType,
    WAVEFORMATEX            **ppDefaultFormat
)
{
    HRESULT hr = S_OK;

    *ppDefaultFormat = NULL;
    if (!VERIFY_SUCCEEDED(hr = GetCachedDefaultFormat(pDevice, eConnectorType, ppDefaultFormat))){
        return hr;
    }
   
    return hr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// GetSupportedFormatsForConnector
//
// For host/keyword detector pin, supported formats info is cached in property store and can be directly read. For offload
// pin, provide with a predefined list of formats and check whether format is supported. For loopback pin, it matches the format
// of host pin so put 0 format record.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT GetSupportedFormatRecordsForConnector
(
    IMMDevice*              pDevice,
    UINT                    uConnectorId,
    EndpointConnectorType   eConnectorType,
    AUDIO_SIGNALPROCESSINGMODE mode,
    STACKWISE_DATAFLOW  dataFlow,
    ULONG *pCount,
    FORMAT_RECORD **ppFormatRecords
)
{
    HRESULT hr = S_OK;

    *pCount = 0;
    *ppFormatRecords = NULL;

    if (eConnectorType == eHostProcessConnector || eConnectorType == eKeywordDetectorConnector) {
        if (!VERIFY_SUCCEEDED(hr = GetCachedSupportedFormatRecords(pDevice, eConnectorType, mode, pCount, ppFormatRecords))) {
            return hr;
        }
    }
    else if (eConnectorType == eOffloadConnector) {
        if (!VERIFY_SUCCEEDED(hr = GetSupportedFormatRecords(pDevice, uConnectorId, eConnectorType, mode, dataFlow, pCount, ppFormatRecords))) {
            return hr;
        }
    }
    else if (eConnectorType == eLoopbackConnector) {
        *pCount = 0;
        *ppFormatRecords = NULL;
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// GetPreferredFormatForConnector
//
// If KSPROPERTY_PIN_PROPOSEDATAFORMAT2 is supported, use the proposed format for mode as preferred format for mode.
// Otherwise, use default format as preferred format.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT GetPreferredFormatForConnector
(
    IMMDevice*                  pDevice,
    UINT                        uConnectorId,
    EndpointConnectorType       eConnectorType,
    AUDIO_SIGNALPROCESSINGMODE  mode,
    WAVEFORMATEX                **ppPreferredFormat
)
{
    HRESULT                                     hr = S_OK;
    wil::unique_cotaskmem_ptr<WAVEFORMATEX>     pDefaultFormat;
    wil::unique_cotaskmem_ptr<WAVEFORMATEX>     pProposedFormat;

    *ppPreferredFormat = NULL;


    // Get default format for connector
    if (!VERIFY_SUCCEEDED(hr = GetDefaultFormatForConnector(pDevice, eConnectorType, wil::out_param(pDefaultFormat)))) {
        return hr;
    }

    // Return the processing mode specific format proposed by the driver
    hr = GetProposedFormatForProcessingMode(pDevice, uConnectorId, mode, wil::out_param(pProposedFormat));
    if (hr == S_OK) {
        CloneWaveFormat(pProposedFormat.get(), ppPreferredFormat);
    }
    else {
        CloneWaveFormat(pDefaultFormat.get(), ppPreferredFormat);
        hr = S_OK;
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// GetPreferredFormatPeriodicityCharacteristicsForConnector
//
// Get periodicity characteristics for format. For host/keyword detector pin, periodicity info is cached along with format
// in property store and can be searched from supported format list. For offload pin, it should be calculate from
// DiscoverPeriodicityCharacteristicsForFormat.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT GetPreferredFormatPeriodicityCharacteristicsForConnector
(
    IMMDevice*                  pDevice,
    EndpointConnectorType       eConnectorType,
    AUDIO_SIGNALPROCESSINGMODE  mode,
    STACKWISE_DATAFLOW          dataFlow,
    WAVEFORMATEX                *pPreferredFormat,
    ULONG                       cFormatRecords,
    PFORMAT_RECORD              pFormatRecords,
    UINT32                      *pDefaultPeriodicityInFrames,
    UINT32                      *pFundamentalPeriodicityInFrames,
    UINT32                      *pMinPeriodicityInFrames,
    UINT32                      *pMaxPeriodicityInFrames,
    UINT32                      *pMaxPeriodicityInFramesExtended
)
{
    HRESULT hr = S_OK;  
    bool    bFormatInList = false;

    for (ULONG i = 0; i < cFormatRecords; i++) {
        if (CompareWaveFormat((WAVEFORMATEX *)&pFormatRecords[i].wfxEx, pPreferredFormat)) {
            bFormatInList = true;
            *pDefaultPeriodicityInFrames = pFormatRecords[i].defaultPeriodInFrames;
            *pFundamentalPeriodicityInFrames = pFormatRecords[i].fundamentalPeriodInFrames;
            *pMinPeriodicityInFrames = pFormatRecords[i].minPeriodInFrames;
            *pMaxPeriodicityInFrames = pFormatRecords[i].maxPeriodInFrames;
            *pMaxPeriodicityInFramesExtended = pFormatRecords[i].maxPeriodInFramesExtended;
            break;
        }
    }

    if (eConnectorType == eHostProcessConnector || eConnectorType == eKeywordDetectorConnector) {
        if (!VERIFY_IS_TRUE(bFormatInList)) {
            hr = E_NOTFOUND;
            return hr;
        }
    }
    else if (eConnectorType == eOffloadConnector || eConnectorType == eLoopbackConnector) {
        if (!VERIFY_SUCCEEDED(hr = DiscoverPeriodicityCharacteristicsForFormat(pDevice, eConnectorType, mode, pPreferredFormat, dataFlow, pDefaultPeriodicityInFrames, pFundamentalPeriodicityInFrames, pMinPeriodicityInFrames, pMaxPeriodicityInFrames, pMaxPeriodicityInFramesExtended))) {
            return hr;
        }
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// AddTestResourceForConnector
//
// For each connector, read connector id, get all processing modes, identify default and preferred format for each mode
// and also enumerate a list of supported formats for each mode. Store all the infos in test resource.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT AddTestResourceForConnector
(
    ResourceList&           resourceList,
    LPWSTR                  deviceId,
    LPWSTR                  deviceName,
    IMMDevice*              pDevice,
    EndpointConnectorType   eConnectorType,
    STACKWISE_DATAFLOW      dataFlow
)
{
    HRESULT                                     hr = S_OK;
    bool                                        bHasConnector = false;
    UINT                                        uConnectorId;
    ULONG                                       cModes = 0;
    CComHeapPtr<AUDIO_SIGNALPROCESSINGMODE>     spModes;
    AUDIO_SIGNALPROCESSINGMODE                  mode;
    ULONG                                       cFormatRecords = 0;
    CComHeapPtr<FORMAT_RECORD>                  spFormatRecords;
    wil::unique_cotaskmem_ptr<WAVEFORMATEX>     pPreferredFormat;
    UINT32                                      u32DefaultPeriodicityInFrames;
    UINT32                                      u32FundamentalPeriodicityInFrames;
    UINT32                                      u32MinPeriodicityInFrames;
    UINT32                                      u32MaxPeriodicityInFrames;
    UINT32                                      u32MaxPeriodicityInFramesExtended;
    bool                                        isPortCls = false;
    bool                                        isAVStream = false;
    bool                                        isBluetooth = false;
    bool                                        isSideband = false;
    bool                                        isMVA = false;

    // Get connector id
    if (!VERIFY_SUCCEEDED(hr = GetConnectorId(pDevice, eConnectorType, &bHasConnector, &uConnectorId))) {
        return hr;
    }
    if (!bHasConnector) {
        return hr;
    }

    Log::Comment(String().Format(L"Adding Test Resource for pin [%u]:", (uConnectorId & PARTID_MASK)));


    // Get all signal processing modes for connector
    if (!VERIFY_SUCCEEDED(hr = GetProcessingModesForConnector(pDevice, uConnectorId, eConnectorType, &cModes, &spModes))) {
        return hr;
    }

    // Loop through each mode, get preferred format and list of formats and create test resource for each mode
    for (ULONG i = 0; i < cModes; i++) {

        mode = spModes[i];
        
        if (!VERIFY_SUCCEEDED(hr = GetSupportedFormatRecordsForConnector(pDevice, uConnectorId, eConnectorType, mode, dataFlow, &cFormatRecords, &spFormatRecords))) {
            return hr;
        }

        if (!VERIFY_SUCCEEDED(hr = GetPreferredFormatForConnector(pDevice, uConnectorId, eConnectorType, mode, wil::out_param(pPreferredFormat)))) {
            return hr;
        }

        if (!VERIFY_SUCCEEDED(hr = GetPreferredFormatPeriodicityCharacteristicsForConnector(pDevice, eConnectorType, mode, dataFlow, pPreferredFormat.get(), cFormatRecords, spFormatRecords, &u32DefaultPeriodicityInFrames, &u32FundamentalPeriodicityInFrames, &u32MinPeriodicityInFrames, &u32MaxPeriodicityInFrames, &u32MaxPeriodicityInFramesExtended))) {
            return hr;
        }

        // Check if audio endpoint is PortCls
        if (!VERIFY_SUCCEEDED(hr = IsPortCls(pDevice, &isPortCls))) {
            return hr;
        }

        // Check if audio endpoint is AVStream
        if (!VERIFY_SUCCEEDED(hr = IsAVStream(pDevice, &isAVStream))) {
            return hr;
        }

        // Check if audio endpoint is Bluetooth
        if (!VERIFY_SUCCEEDED(hr = IsBluetooth(pDevice, &isBluetooth))) {
            return hr;
        }

        // Check if audio endpoint is side band
        if (!VERIFY_SUCCEEDED(hr = IsSideband(pDevice, &isSideband))) {
            return hr;
        }

        // Check if audio endpoint is MVA
        if (!VERIFY_SUCCEEDED(hr = IsMVA(eConnectorType, pDevice, &isMVA))) {
            return hr;
        }

        DeviceDescriptor descriptor = { 0 };
        descriptor.pDevice = pDevice;
        descriptor.pwstrAudioEndpointId = deviceId;
        descriptor.pwstrAudioEndpointFriendlyName = deviceName;
        descriptor.dataFlow = dataFlow;
        descriptor.eConnectorType = eConnectorType;
        descriptor.uConnectorId = uConnectorId;
        descriptor.mode = mode;
        descriptor.cModes = cModes;
        descriptor.pModes = spModes;
        descriptor.cFormatRecords = cFormatRecords;
        descriptor.pFormatRecords = spFormatRecords;
        descriptor.pPreferredFormat = pPreferredFormat.get();
        descriptor.u32DefaultPeriodicityInFrames = u32DefaultPeriodicityInFrames;
        descriptor.u32FundamentalPeriodicityInFrames = u32FundamentalPeriodicityInFrames;
        descriptor.u32MinPeriodicityInFrames = u32MinPeriodicityInFrames;
        descriptor.u32MaxPeriodicityInFrames = u32MaxPeriodicityInFrames;
        descriptor.bIsPortCls = isPortCls;
        descriptor.bIsAVStream = isAVStream;
        descriptor.bIsBluetooth = isBluetooth;
        descriptor.bIsSideband = isSideband;
        descriptor.bIsMVA = isMVA;

        if (!VERIFY_SUCCEEDED(hr = CreateTestResource(resourceList, descriptor))) {
            return hr;
        }

        spFormatRecords.Free();
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// AddTestResourcesForDevice
//
// Identify the existence of all pin types and add test resources for each pin 
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT AddTestResourcesForDevice
(
    ResourceList&       resourceList,
    LPWSTR              deviceId,
    LPWSTR              deviceName,
    STACKWISE_DATAFLOW  dataFlow
)
{
    HRESULT                                     hr = S_OK;
    wil::com_ptr_nothrow<IMMDeviceEnumerator>   spEnumerator;
    wil::com_ptr_nothrow<IMMDevice>             spDevice;

    Log::Comment(String().Format(L"Adding Test Resource for Device [%s]:", deviceName));

    // Read the default device format from the property store
    if (!VERIFY_SUCCEEDED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&spEnumerator))) {
        return hr;
    }
    if (!VERIFY_SUCCEEDED(hr = spEnumerator->GetDevice(deviceId, &spDevice))) {
        return hr;
    }

    // Identify existence of host pin. Add test resources for host pin.
    if (!VERIFY_SUCCEEDED(hr = AddTestResourceForConnector(resourceList, deviceId, deviceName, spDevice.get(), eHostProcessConnector, dataFlow))) {
        return hr;
    }

    // Identify existence of offload pin. Add test resources for offload pin.
    if (!VERIFY_SUCCEEDED(hr = AddTestResourceForConnector(resourceList, deviceId, deviceName, spDevice.get(), eOffloadConnector, dataFlow))) {
        return hr;
    }
    
    // Identify existence of loopback pin. Add test resources for loopback pin. 
    if (!VERIFY_SUCCEEDED(hr = AddTestResourceForConnector(resourceList, deviceId, deviceName, spDevice.get(), eLoopbackConnector, capture ))) {
        return hr;
    }

    // Identify existence of keyword detector pin. Add test resources for keyword detector pin.
    if (!VERIFY_SUCCEEDED(hr = AddTestResourceForConnector(resourceList, deviceId, deviceName, spDevice.get(), eKeywordDetectorConnector, dataFlow))) {
        return hr;
    }

    return hr;
}

HRESULT AddDevices(ResourceList& resourceList)
{
    HRESULT hr = S_OK;
    wil::com_ptr_nothrow<IMMDeviceEnumerator>    spEnumerator;
    wil::com_ptr_nothrow<IMMDeviceCollection>    spEndpoints;
    wil::com_ptr_nothrow<IMMDevice>              spEndpoint;
    UINT                                         cDevices = 0;
    UINT                                         i = 0;
    wil::unique_cotaskmem_string                 id;
    wil::unique_cotaskmem_string                 friendlyName;
    EDataFlow                                    endpointDataFlow;

    SetVerifyOutput verifySettings(VerifyOutputSettings::LogOnlyFailures);
    DisableVerifyExceptions disable;

    VERIFY_SUCCEEDED(::CoInitializeEx(NULL, COINIT_MULTITHREADED));

    Log::Comment(L"In BuildResourceList");

    // Create IMMDevice Enumerator
    VERIFY_SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&spEnumerator));

    // Enumerate all endpoints
    VERIFY_SUCCEEDED(spEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &spEndpoints));
    VERIFY_SUCCEEDED(spEndpoints->GetCount(&cDevices));

    if (!VERIFY_IS_TRUE(cDevices))
    {
        Log::Comment(L"No device was found!");
        return E_FAIL;
    }

    // Check if a device ID was specified.
    String instanceId;
    BOOL isDeviceSelected = false;
    HRESULT res = RuntimeParameters::TryGetValue(L"InstanceId", instanceId);
    if (res == S_OK)
    {
        Log::Comment(String().Format(L"Selected device ID: %s", static_cast<LPCWSTR>(instanceId)));
        isDeviceSelected = true;
    }

    Log::Comment(String().Format(L"Found %d viable endpoint(s)!", cDevices));
    // Add test resources for endpoints
    for (i = 0; i < cDevices; i++) {
        VERIFY_SUCCEEDED(spEndpoints->Item(i, &spEndpoint));
        VERIFY_SUCCEEDED(spEndpoint->GetId(&id));
        VERIFY_SUCCEEDED(GetEndpointFriendlyName(spEndpoint.get(), &friendlyName));

        // Check whether it is a render or capture endpoint.
        CComPtr<IMMEndpoint> pMmEndpoint;
        VERIFY_SUCCEEDED(spEndpoint->QueryInterface(__uuidof(IMMEndpoint), (void**)&pMmEndpoint));
        pMmEndpoint->GetDataFlow(&endpointDataFlow);

        // If there is a device selected, check if the current endpoint belongs to the device.
        if (isDeviceSelected)
        {
            // Get the IDeviceTopology interface of the endpoint.
            CComPtr<IDeviceTopology> pEndpointTopology;
            if (!VERIFY_SUCCEEDED(hr = spEndpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, NULL, (void**)&pEndpointTopology))) { return hr; }

            // Get the connector inside the device topology object.
            CComPtr<IConnector> spConnector;
            if (!VERIFY_SUCCEEDED(hr = pEndpointTopology->GetConnector(0, &spConnector))) { return hr; }

            // Get the id of the device adapter this endpoint is connected to.
            CComHeapPtr<WCHAR> szFilterId;
            if (!VERIFY_SUCCEEDED(hr = spConnector->GetDeviceIdConnectedTo(&szFilterId))) { return hr; }

            // Get the IMMDevice object of this device adapter.
            CComPtr<IMMDevice> spDevnode;
            if (!VERIFY_SUCCEEDED(hr = spEnumerator->GetDevice(szFilterId, &spDevnode))) { return hr; }

            // Open the property store and get the instance ID of the device adapter.
            PROPVARIANT varInstanceId;
            PropVariantInit(&varInstanceId);

            CComPtr<IPropertyStore> spDevnodePropertyStore;
            if (!VERIFY_SUCCEEDED(hr = spDevnode->OpenPropertyStore(STGM_READ, &spDevnodePropertyStore))) { return hr; }

            if (!VERIFY_SUCCEEDED(hr = spDevnodePropertyStore->GetValue(PKEY_Device_InstanceId, &varInstanceId))) { return hr; }
            if (!VERIFY_ARE_EQUAL(varInstanceId.vt, VT_LPWSTR)) { return E_UNEXPECTED; }

            // If this endpoint does not belong to the selected device, skip it.
            if (0 != _wcsicmp(instanceId, varInstanceId.pwszVal))
            {
                continue;
            }
        }
        Log::Comment(String().Format(L"\\\\ Device: %s (%s)", friendlyName.get(), id.get()));
        VERIFY_SUCCEEDED(AddTestResourcesForDevice(resourceList, id.get(), friendlyName.get(), endpointDataFlow == eRender ? render : capture));
    }

    Log::Comment(String().Format(L"Enumerated %u resources", resourceList.Count()));

    return hr;
}
