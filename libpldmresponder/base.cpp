#include "config.h"

#include "libpldm/base.h"

#include "libpldm/bios.h"
#include "libpldm/fru.h"
#include "libpldm/platform.h"
#include "libpldm/requester/pldm.h"

#include "base.hpp"
#include "common/utils.hpp"
#include "libpldmresponder/pdr.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#ifdef OEM_IBM
#include "libpldm/file_io.h"
#include "libpldm/host.h"
#endif

namespace pldm
{

using Type = uint8_t;

namespace responder
{

using Cmd = std::vector<uint8_t>;

static const std::map<Type, Cmd> capabilities{
    {PLDM_BASE,
     {PLDM_GET_TID, PLDM_GET_PLDM_VERSION, PLDM_GET_PLDM_TYPES,
      PLDM_GET_PLDM_COMMANDS}},
    {PLDM_PLATFORM,
     {PLDM_GET_PDR, PLDM_SET_STATE_EFFECTER_STATES, PLDM_SET_EVENT_RECEIVER,
      PLDM_GET_SENSOR_READING, PLDM_GET_STATE_SENSOR_READINGS,
      PLDM_SET_NUMERIC_EFFECTER_VALUE, PLDM_GET_NUMERIC_EFFECTER_VALUE,
      PLDM_PLATFORM_EVENT_MESSAGE}},
    {PLDM_BIOS,
     {PLDM_GET_DATE_TIME, PLDM_SET_DATE_TIME, PLDM_GET_BIOS_TABLE,
      PLDM_GET_BIOS_ATTRIBUTE_CURRENT_VALUE_BY_HANDLE,
      PLDM_SET_BIOS_ATTRIBUTE_CURRENT_VALUE, PLDM_SET_BIOS_TABLE}},
    {PLDM_FRU,
     {PLDM_GET_FRU_RECORD_TABLE_METADATA, PLDM_GET_FRU_RECORD_TABLE,
      PLDM_GET_FRU_RECORD_BY_OPTION}},
#ifdef OEM_IBM
    {PLDM_OEM,
     {PLDM_HOST_GET_ALERT_STATUS, PLDM_GET_FILE_TABLE, PLDM_READ_FILE,
      PLDM_WRITE_FILE, PLDM_READ_FILE_INTO_MEMORY, PLDM_WRITE_FILE_FROM_MEMORY,
      PLDM_READ_FILE_BY_TYPE_INTO_MEMORY, PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY,
      PLDM_NEW_FILE_AVAILABLE, PLDM_READ_FILE_BY_TYPE, PLDM_WRITE_FILE_BY_TYPE,
      PLDM_FILE_ACK}},
#endif
};

static const std::map<Type, ver32_t> versions{
    {PLDM_BASE, {0x00, 0xf0, 0xf0, 0xf1}},
    {PLDM_PLATFORM, {0x00, 0xf0, 0xf2, 0xf1}},
    {PLDM_BIOS, {0x00, 0xf0, 0xf0, 0xf1}},
    {PLDM_FRU, {0x00, 0xf0, 0xf0, 0xf1}},
#ifdef OEM_IBM
    {PLDM_OEM, {0x00, 0xf0, 0xf0, 0xf1}},
#endif
};

namespace base
{

Response Handler::getPLDMTypes(const pldm_msg* request,
                               size_t /*payloadLength*/)
{
    // DSP0240 has this as a bitfield8[N], where N = 0 to 7
    std::array<bitfield8_t, 8> types{};
    for (const auto& type : capabilities)
    {
        auto index = type.first / 8;
        // <Type Number> = <Array Index> * 8 + <bit position>
        auto bit = type.first - (index * 8);
        types[index].byte |= 1 << bit;
    }

    Response response(sizeof(pldm_msg_hdr) + PLDM_GET_TYPES_RESP_BYTES, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    auto rc = encode_get_types_resp(request->hdr.instance_id, PLDM_SUCCESS,
                                    types.data(), responsePtr);
    if (rc != PLDM_SUCCESS)
    {
        return CmdHandler::ccOnlyResponse(request, rc);
    }

    return response;
}

Response Handler::getPLDMCommands(const pldm_msg* request, size_t payloadLength)
{
    ver32_t version{};
    Type type;

    Response response(sizeof(pldm_msg_hdr) + PLDM_GET_COMMANDS_RESP_BYTES, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    auto rc = decode_get_commands_req(request, payloadLength, &type, &version);

    if (rc != PLDM_SUCCESS)
    {
        return CmdHandler::ccOnlyResponse(request, rc);
    }

    // DSP0240 has this as a bitfield8[N], where N = 0 to 31
    std::array<bitfield8_t, 32> cmds{};
    if (capabilities.find(type) == capabilities.end())
    {
        return CmdHandler::ccOnlyResponse(request,
                                          PLDM_ERROR_INVALID_PLDM_TYPE);
    }

    for (const auto& cmd : capabilities.at(type))
    {
        auto index = cmd / 8;
        // <Type Number> = <Array Index> * 8 + <bit position>
        auto bit = cmd - (index * 8);
        cmds[index].byte |= 1 << bit;
    }

    rc = encode_get_commands_resp(request->hdr.instance_id, PLDM_SUCCESS,
                                  cmds.data(), responsePtr);
    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    return response;
}

Response Handler::getPLDMVersion(const pldm_msg* request, size_t payloadLength)
{
    uint32_t transferHandle;
    Type type;
    uint8_t transferFlag;

    Response response(sizeof(pldm_msg_hdr) + PLDM_GET_VERSION_RESP_BYTES, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    uint8_t rc = decode_get_version_req(request, payloadLength, &transferHandle,
                                        &transferFlag, &type);

    if (rc != PLDM_SUCCESS)
    {
        return CmdHandler::ccOnlyResponse(request, rc);
    }

    ver32_t version{};
    auto search = versions.find(type);

    if (search == versions.end())
    {
        return CmdHandler::ccOnlyResponse(request,
                                          PLDM_ERROR_INVALID_PLDM_TYPE);
    }

    memcpy(&version, &(search->second), sizeof(version));
    rc = encode_get_version_resp(request->hdr.instance_id, PLDM_SUCCESS, 0,
                                 PLDM_START_AND_END, &version,
                                 sizeof(pldm_version), responsePtr);
    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    return response;
}

void Handler::processSetEventReceiver(
    sdeventplus::source::EventBase& /*source */)
{
    survEvent.reset();
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_SET_EVENT_RECEIVER_REQ_BYTES);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto instanceId = requester.getInstanceId(eid);
    uint8_t eventMessageGlobalEnable =
        PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC_KEEP_ALIVE;
    uint8_t transportProtocolType = PLDM_TRANSPORT_PROTOCOL_TYPE_MCTP;
    uint8_t eventReceiverAddressInfo = pldm::responder::pdr::BmcMctpEid;
    uint16_t heartbeatTimer = HEARTBEAT_TIMEOUT;

    auto rc = encode_set_event_receiver_req(
        instanceId, eventMessageGlobalEnable, transportProtocolType,
        eventReceiverAddressInfo, heartbeatTimer, request);
    if (rc != PLDM_SUCCESS)
    {
        requester.markFree(eid, instanceId);
        std::cerr << "Failed to encode_set_event_receiver_req, rc = "
                  << std::hex << std::showbase << rc << std::endl;
        return;
    }

    auto processSetEventReceiverResponse = [](mctp_eid_t /*eid*/,
                                              const pldm_msg* response,
                                              size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            std::cerr << "Failed to receive response for "
                         "setEventReceiver command \n";
            return;
        }

        uint8_t completionCode{};
        auto rc = decode_set_event_receiver_resp(response, respMsgLen,
                                                 &completionCode);
        if (rc || completionCode)
        {
            std::cerr << "Failed to decode setEventReceiver command response,"
                      << " rc=" << rc << "cc=" << (unsigned)completionCode
                      << "\n";
            pldm::utils::reportError(
                "xyz.openbmc_project.bmc.pldm.InternalFailure");
        }
    };
    rc = handler->registerRequest(
        eid, instanceId, PLDM_PLATFORM, PLDM_SET_EVENT_RECEIVER,
        std::move(requestMsg), std::move(processSetEventReceiverResponse));

    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "Failed to send the setEventReceiver request"
                  << "\n";
    }

    if (oemPlatformHandler)
    {
        oemPlatformHandler->countSetEventReceiver();
        oemPlatformHandler->checkAndDisableWatchDog();
    }
}

Response Handler::getTID(const pldm_msg* request, size_t /*payloadLength*/)
{
    // assigned 1 to the bmc as the PLDM terminus
    uint8_t tid = 1;

    Response response(sizeof(pldm_msg_hdr) + PLDM_GET_TID_RESP_BYTES, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    auto rc = encode_get_tid_resp(request->hdr.instance_id, PLDM_SUCCESS, tid,
                                  responsePtr);
    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    survEvent = std::make_unique<sdeventplus::source::Defer>(
        event, std::bind_front(&Handler::processSetEventReceiver, this));

    return response;
}

} // namespace base
} // namespace responder
} // namespace pldm
