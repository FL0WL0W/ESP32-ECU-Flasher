#include "ECUService_GMCAN.h"
#include "ICommunicationService.h"

#ifndef IECUSERVICE_E38_H
#define IECUSERVICE_E38_H

class ECUService_E38 : public ECUService_GMCAN
{
    protected:
        ECUServiceErrorCode SeedKeyAlgorithm(uint8_t *key, const uint8_t *seed, uint8_t seedLength) override;
    public:
        ECUService_E38(EmbeddedIOServices::ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay);
        ~ECUService_E38() override;
};

#endif