
#pragma once

class BmcWebHooks
{
  public:
    virtual void registerRoutes() = 0; // Pure virtual function
    virtual ~BmcWebHooks() = default;  // Virtual destructor
};
