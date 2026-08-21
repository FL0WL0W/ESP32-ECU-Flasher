#include "ECUService_GMCAN.h"
#include "ICommunicationService.h"

#ifndef IECUSERVICE_E78_H
#define IECUSERVICE_E78_H

class ECUService_E78 : public ECUService_GMCAN
{
    protected:
        ECUServiceErrorCode SeedKeyAlgorithm(uint8_t *key, const uint8_t *seed, uint8_t seedLength) override;
    public:
        ECUService_E78(EmbeddedIOServices::ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay);
        ~ECUService_E78() override;
};

#endif