
#pragma once
#include <string>
class BmcWebHooks
{
  public:
    virtual std::string registerRoutes() = 0; // Pure virtual function
    virtual ~BmcWebHooks() = default;  // Virtual destructor
};
