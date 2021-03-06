/*=====================================================================
 
 QGroundControl Open Source Ground Control Station
 
 (c) 2009 - 2014 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 
 This file is part of the QGROUNDCONTROL project
 
 QGROUNDCONTROL is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 QGROUNDCONTROL is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with QGROUNDCONTROL. If not, see <http://www.gnu.org/licenses/>.
 
 ======================================================================*/

#include "MockLink.h"

#include <QTimer>
#include <QDebug>
#include <QFile>

#include <string.h>

/// @file
///     @brief Mock implementation of a Link.
///
///     @author Don Gagne <don@thegagnes.com>

MockLink::MockLink(void) :
    _linkId(getNextLinkId()),
    _name("MockLink"),
    _connected(false),
    _vehicleSystemId(128),     // FIXME: Pull from eventual parameter manager
    _vehicleComponentId(200),  // FIXME: magic number?
    _inNSH(false),
    _mavlinkStarted(false),
    _mavMode(MAV_MODE_FLAG_MANUAL_INPUT_ENABLED),
    _mavState(MAV_STATE_STANDBY)
{
    _missionItemHandler = new MockLinkMissionItemHandler(_vehicleSystemId, this);
    Q_CHECK_PTR(_missionItemHandler);
    
    moveToThread(this);
    _loadParams();
    QObject::connect(this, &MockLink::_incomingBytes, this, &MockLink::_handleIncomingBytes);
}

MockLink::~MockLink(void)
{
    _disconnect();
    deleteLater();
}

void MockLink::readBytes(void)
{
    // FIXME: This is a bad virtual from LinkInterface?
}

bool MockLink::_connect(void)
{
    _connected = true;
    
    start();
    
    emit connected();
    emit connected(true);
    
    return true;
}

bool MockLink::_disconnect(void)
{
    _connected = false;
    exit();
    
    emit disconnected();
    emit connected(false);
    
    return true;
}

void MockLink::run(void)
{
    QTimer  _timer1HzTasks;
    QTimer  _timer10HzTasks;
    QTimer  _timer50HzTasks;
    
    QObject::connect(&_timer1HzTasks, &QTimer::timeout, this, &MockLink::_run1HzTasks);
    QObject::connect(&_timer10HzTasks, &QTimer::timeout, this, &MockLink::_run10HzTasks);
    QObject::connect(&_timer50HzTasks, &QTimer::timeout, this, &MockLink::_run50HzTasks);
    
    _timer1HzTasks.start(1000);
    _timer10HzTasks.start(100);
    _timer50HzTasks.start(20);
    
    exec();
    
    emit disconnected();
    emit connected(false);
}

void MockLink::_run1HzTasks(void)
{
    if (_mavlinkStarted) {
        _sendHeartBeat();
    }
}

void MockLink::_run10HzTasks(void)
{
    if (_mavlinkStarted) {
    }
}

void MockLink::_run50HzTasks(void)
{
    if (_mavlinkStarted) {
    }
}

void MockLink::_loadParams(void)
{
    QFile paramFile(":/unittest/MockLink.param");
    
    bool success = paramFile.open(QFile::ReadOnly);
    Q_UNUSED(success);
    Q_ASSERT(success);
    
    QTextStream paramStream(&paramFile);
    
    while (!paramStream.atEnd()) {
        QString line = paramStream.readLine();
        
        if (line.startsWith("#")) {
            continue;
        }
        
        QStringList paramData = line.split("\t");
        Q_ASSERT(paramData.count() == 5);
        
        QString paramName = paramData.at(2);
        QString valStr = paramData.at(3);
        uint paramType = paramData.at(4).toUInt();
        
        QVariant paramValue;
        switch (paramType) {
            case MAV_PARAM_TYPE_REAL32:
                paramValue = QVariant(valStr.toFloat());
                break;
            case MAV_PARAM_TYPE_UINT32:
                paramValue = QVariant(valStr.toUInt());
                break;
            case MAV_PARAM_TYPE_INT32:
                paramValue = QVariant(valStr.toInt());
                break;
            case MAV_PARAM_TYPE_INT8:
                paramValue = QVariant((unsigned char)valStr.toUInt());
                break;
            default:
                Q_ASSERT(false);
                break;
        }
        
        _parameters[paramName] = paramValue;
    }
    _cParameters = _parameters.count();
}

void MockLink::_sendHeartBeat(void)
{
    mavlink_message_t   msg;
    uint8_t             buffer[MAVLINK_MAX_PACKET_LEN];

    mavlink_msg_heartbeat_pack(_vehicleSystemId,
                               _vehicleComponentId,
                               &msg,
                               MAV_TYPE_QUADROTOR,  // MAV_TYPE
                               MAV_AUTOPILOT_PX4,   // MAV_AUTOPILOT
                               _mavMode,            // MAV_MODE
                               0,                   // custom mode
                               _mavState);          // MAV_STATE

    int cBuffer = mavlink_msg_to_send_buffer(buffer, &msg);
    QByteArray bytes((char *)buffer, cBuffer);
    emit bytesReceived(this, bytes);
}

/// @brief Called when QGC wants to write bytes to the MAV
void MockLink::writeBytes(const char* bytes, qint64 cBytes)
{
    // Package up the data so we can signal it over to the right thread
    QByteArray byteArray(bytes, cBytes);
    
    emit _incomingBytes(byteArray);
}

/// @brief Handles bytes from QGC on the thread
void MockLink::_handleIncomingBytes(const QByteArray bytes)
{
    if (_inNSH) {
        _handleIncomingNSHBytes(bytes.constData(), bytes.count());
    } else {
        if (bytes.startsWith(QByteArray("\r\r\r"))) {
            _inNSH  = true;
            _handleIncomingNSHBytes(&bytes.constData()[3], bytes.count() - 3);
        }
        
        _handleIncomingMavlinkBytes((uint8_t *)bytes.constData(), bytes.count());
    }
}

/// @brief Handle incoming bytes which are meant to be interpreted by the NuttX shell
void MockLink::_handleIncomingNSHBytes(const char* bytes, int cBytes)
{
    Q_UNUSED(cBytes);
    
    // Drop back out of NSH
    if (cBytes == 4 && bytes[0] == '\r' && bytes[1] == '\r' && bytes[2] == '\r') {
        _inNSH  = false;
        return;
    }

    if (cBytes > 0) {
        qDebug() << "NSH:" << (const char*)bytes;
        
        if (strncmp(bytes, "sh /etc/init.d/rc.usb\n", cBytes) == 0) {
            // This is the mavlink start command
            _mavlinkStarted = true;
        }
    }
}

/// @brief Handle incoming bytes which are meant to be handled by the mavlink protocol
void MockLink::_handleIncomingMavlinkBytes(const uint8_t* bytes, int cBytes)
{
    mavlink_message_t msg;
    mavlink_status_t comm;
    
    for (qint64 i=0; i<cBytes; i++)
    {
        if (!mavlink_parse_char(_linkId, bytes[i], &msg, &comm)) {
            continue;
        }
        
        Q_ASSERT(_missionItemHandler);
        _missionItemHandler->handleMessage(msg);
        
        switch (msg.msgid) {
            case MAVLINK_MSG_ID_HEARTBEAT:
                _handleHeartBeat(msg);
                break;
                
            case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
                _handleParamRequestList(msg);
                break;
                
            case MAVLINK_MSG_ID_SET_MODE:
                _handleSetMode(msg);
                break;
                
            case MAVLINK_MSG_ID_PARAM_SET:
                _handleParamSet(msg);
                break;
                
            case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
                _handleParamRequestRead(msg);
                break;
                
            case MAVLINK_MSG_ID_MISSION_REQUEST_LIST:
                _handleMissionRequestList(msg);
                break;
                
            case MAVLINK_MSG_ID_MISSION_REQUEST:
                _handleMissionRequest(msg);
                break;
                
            case MAVLINK_MSG_ID_MISSION_ITEM:
                _handleMissionItem(msg);
                break;
                
#if 0
            case MAVLINK_MSG_ID_MISSION_COUNT:
                _handleMissionCount(msg);
                break;
#endif
                
            default:
                qDebug() << "MockLink: Unhandled mavlink message, id:" << msg.msgid;
                break;
        }
    }
}

void MockLink::_emitMavlinkMessage(const mavlink_message_t& msg)
{
    uint8_t outputBuffer[MAVLINK_MAX_PACKET_LEN];
    
    int cBuffer = mavlink_msg_to_send_buffer(outputBuffer, &msg);
    QByteArray bytes((char *)outputBuffer, cBuffer);
    emit bytesReceived(this, bytes);
}

void MockLink::_handleHeartBeat(const mavlink_message_t& msg)
{
    Q_UNUSED(msg);
#if 0
    mavlink_heartbeat_t heartbeat;
    mavlink_msg_heartbeat_decode(&msg, &heartbeat);
#endif
}

void MockLink::_handleSetMode(const mavlink_message_t& msg)
{
    mavlink_set_mode_t request;
    mavlink_msg_set_mode_decode(&msg, &request);
    
    if (request.target_system == _vehicleSystemId) {
        _mavMode = request.base_mode;
    } else {
        _errorInvalidTargetSystem(request.target_system);
    }
}

void MockLink::_errorInvalidTargetSystem(int targetId)
{
    QString errMsg("MSG_ID_SET_MODE received incorrect target system: actual(%1) expected(%2)");
    emit error(errMsg.arg(targetId).arg(_vehicleSystemId));
}

void MockLink::_handleParamRequestList(const mavlink_message_t& msg)
{
    mavlink_param_request_list_t request;
    
    mavlink_msg_param_request_list_decode(&msg, &request);
    
    uint16_t paramIndex = 0;
    if (request.target_system == _vehicleSystemId) {
        ParamMap_t::iterator param;
        
        for (param = _parameters.begin(); param != _parameters.end(); param++) {
            mavlink_message_t   responseMsg;

            mavlink_msg_param_value_pack(_vehicleSystemId,
                                         _vehicleComponentId,
                                         &responseMsg,                          // Outgoing message
                                         param.key().toLocal8Bit().constData(), // Parameter name
                                         param.value().toFloat(),               // Parameter vluae
                                         MAV_PARAM_TYPE_REAL32,                 // FIXME: Pull from QVariant type
                                         _cParameters,                          // Total number of parameters
                                         paramIndex++);                         // Index of this parameter
            _emitMavlinkMessage(responseMsg);
        }
    } else {
        _errorInvalidTargetSystem(request.target_system);
    }
}

void MockLink::_handleParamSet(const mavlink_message_t& msg)
{
    mavlink_param_set_t request;
    mavlink_msg_param_set_decode(&msg, &request);
    
    if (request.target_system == _vehicleSystemId) {
        // Param may not be null terminated if exactly fits
        char paramId[MAVLINK_MSG_PARAM_SET_FIELD_PARAM_ID_LEN + 1];
        strncpy(paramId, request.param_id, MAVLINK_MSG_PARAM_SET_FIELD_PARAM_ID_LEN);
        
        if (_parameters.contains(paramId))
        {
            _parameters[paramId] = request.param_value;
            
            mavlink_message_t   responseMsg;
            
            mavlink_msg_param_value_pack(_vehicleSystemId,
                                         _vehicleComponentId,
                                         &responseMsg,                          // Outgoing message
                                         paramId,                               // Parameter name
                                         request.param_value,                   // Parameter vluae
                                         MAV_PARAM_TYPE_REAL32,                 // FIXME: Pull from QVariant type
                                         _cParameters,                          // Total number of parameters
                                         _parameters.keys().indexOf(paramId));  // Index of this parameter
            _emitMavlinkMessage(responseMsg);
        } else {
            QString errMsg("MSG_ID_PARAM_SET requested unknown param id (%1)");
            emit error(errMsg.arg(paramId));
        }
    } else {
        _errorInvalidTargetSystem(request.target_system);
    }
}

void MockLink::_handleParamRequestRead(const mavlink_message_t& msg)
{
    mavlink_param_request_read_t request;
    mavlink_msg_param_request_read_decode(&msg, &request);
    
    char paramId[MAVLINK_MSG_PARAM_REQUEST_READ_FIELD_PARAM_ID_LEN + 1];
    paramId[0] = 0;
    
    if (request.target_system == _vehicleSystemId) {
        if (request.param_index == -1) {
            // Request is by param name. Param may not be null terminated if exactly fits
            strncpy(paramId, request.param_id, MAVLINK_MSG_PARAM_REQUEST_READ_FIELD_PARAM_ID_LEN);
        } else {
            if (request.param_index >= 0 && request.param_index < _cParameters) {
                // Request is by index
                QString key = _parameters.keys().at(request.param_index);
                Q_ASSERT(key.length() <= MAVLINK_MSG_PARAM_REQUEST_READ_FIELD_PARAM_ID_LEN);
                strcpy(paramId, key.toLocal8Bit().constData());
            } else {
                QString errMsg("MSG_ID_PARAM_REQUEST_READ requested unknown index: requested(%1) count(%2)");
                emit error(errMsg.arg(request.param_index).arg(_cParameters));
            }
        }
            
        if (paramId[0] && _parameters.contains(paramId)) {
            float paramValue = _parameters[paramId].toFloat();
            
            mavlink_message_t   responseMsg;
            
            mavlink_msg_param_value_pack(_vehicleSystemId,
                                         _vehicleComponentId,
                                         &responseMsg,                          // Outgoing message
                                         paramId,                               // Parameter name
                                         paramValue,                            // Parameter vluae
                                         MAV_PARAM_TYPE_REAL32,                 // FIXME: Pull from QVariant type
                                         _cParameters,                          // Total number of parameters
                                         _parameters.keys().indexOf(paramId));  // Index of this parameter
            _emitMavlinkMessage(responseMsg);
        }
    } else {
        _errorInvalidTargetSystem(request.target_system);
    }
}

void MockLink::_handleMissionRequestList(const mavlink_message_t& msg)
{
    mavlink_mission_request_list_t request;
    
    mavlink_msg_mission_request_list_decode(&msg, &request);
    
    if (request.target_system == _vehicleSystemId) {
        mavlink_message_t   responseMsg;

        mavlink_msg_mission_count_pack(_vehicleSystemId,
                                       _vehicleComponentId,
                                       &responseMsg,            // Outgoing message
                                       msg.sysid,               // Target is original sender
                                       msg.compid,              // Target is original sender
                                       _missionItems.count());  // Number of mission items
        _emitMavlinkMessage(responseMsg);
    } else {
        _errorInvalidTargetSystem(request.target_system);
    }
}

void MockLink::_handleMissionRequest(const mavlink_message_t& msg)
{
    mavlink_mission_request_t request;
    
    mavlink_msg_mission_request_decode(&msg, &request);
    
    if (request.target_system == _vehicleSystemId) {
        if (request.seq < _missionItems.count()) {
            mavlink_message_t   responseMsg;

            mavlink_mission_item_t item = _missionItems[request.seq];

            mavlink_msg_mission_item_pack(_vehicleSystemId,
                                          _vehicleComponentId,
                                          &responseMsg,            // Outgoing message
                                          msg.sysid,               // Target is original sender
                                          msg.compid,              // Target is original sender
                                          request.seq,             // Index of mission item being sent
                                          item.frame,
                                          item.command,
                                          item.current,
                                          item.autocontinue,
                                          item.param1, item.param2, item.param3, item.param4,
                                          item.x, item.y, item.z);
            _emitMavlinkMessage(responseMsg);
        } else {
            QString errMsg("MSG_ID_MISSION_REQUEST requested unknown sequence number: requested(%1) count(%2)");
            emit error(errMsg.arg(request.seq).arg(_missionItems.count()));
        }
    } else {
        _errorInvalidTargetSystem(request.target_system);
    }
}

void MockLink::_handleMissionItem(const mavlink_message_t& msg)
{
    mavlink_mission_item_t request;
    
    mavlink_msg_mission_item_decode(&msg, &request);
    
    if (request.target_system == _vehicleSystemId) {
        // FIXME: What do you do with duplication sequence numbers?
        Q_ASSERT(!_missionItems.contains(request.seq));
        
        _missionItems[request.seq] = request;
    } else {
        _errorInvalidTargetSystem(request.target_system);
    }
}
