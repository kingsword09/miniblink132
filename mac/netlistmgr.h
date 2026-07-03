#ifndef MAC_NETLISTMGR_H
#define MAC_NETLISTMGR_H

#include "unknwn.h"

class INetworkListManager : public IUnknown {
public:
    virtual HRESULT get_IsConnectedToInternet(VARIANT_BOOL* pbIsConnected) = 0;
};

#endif // MAC_NETLISTMGR_H
