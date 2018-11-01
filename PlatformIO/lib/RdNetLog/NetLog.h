// NetLog
// Used to log data to various places (MQTT, commandSerial, HTTP) from ArduinoLog module
// Rob Dobson 2018

#pragma once

#include <ArduinoLog.h>
#include <WiFiClient.h>
#include "MQTTManager.h"
#include "CommandSerial.h"
#include "RdRingBufferPosn.h"

class NetLog : public Print
{
public:
    // Used to pause and resume logging
    static constexpr char ASCII_XOFF = 0x13;
    static constexpr char ASCII_XON = 0x11;

private:
    // Log message needs to be built up from parts
    String _msgToLog;
    bool _firstChOnLine;
    bool _collectLineForLog;
    int _curMsgLogLevel;
    const int LOG_LINE_MAXLEN = 250;

    // Logging goes to this sink always
    Print& _output;

    // Logging destinations
    MQTTManager& _mqttManager;
    bool _logToMQTT;
    String _mqttLogTopic;
    bool _logToHTTP;
    String _httpIpAddr;
    int _httpPort;
    String _httpLogUrl;
    int _logToSerial;
    int _serialPort;
    int _logToCommandSerial;
    CommandSerial& _commandSerial;
    
    // Logging control
    int _loggingThreshold;
    
    // Configuration (object held elsewhere and pointer kept
    // to allow config changes to be written back)
    ConfigBase *_pConfigBase;

    // TCP client
    WiFiClient _wifiClient;
    const int MAX_RX_BUFFER_SIZE = 100;

    // System name
    String _systemName;

    // Pause functionality
    bool _isPaused;
    uint32_t _pauseTimeMs;
    uint32_t _pauseStartedMs;
    uint8_t *_pChBuffer;
    RingBufferPosn _chBufferPosn;

  public:
    NetLog(Print& output, MQTTManager& mqttManager, CommandSerial& commandSerial,
            int pauseBufferMaxChars = 1000, uint32_t pauseTimeMs = 15000) :
        _output(output),
        _mqttManager(mqttManager),
        _commandSerial(commandSerial),
        _chBufferPosn(pauseBufferMaxChars)
    {
        _firstChOnLine = true;
        _collectLineForLog = false;
        _msgToLog.reserve(250);
        _curMsgLogLevel = LOG_LEVEL_SILENT;
        _loggingThreshold = LOG_LEVEL_SILENT;
        _logToMQTT = false;
        _logToHTTP = false;
        _pConfigBase = NULL;
        _httpPort = 5076;
        _logToSerial = true;
        _logToCommandSerial = false;
        _serialPort = 0;
        _pauseStartedMs = 0;
        _pauseTimeMs = pauseTimeMs;
        _isPaused = false;
        _pChBuffer = new uint8_t[pauseBufferMaxChars];
    }

    void setLogLevel(const char* logLevelStr)
    {
        // Get level
        int logLevel = LOG_LEVEL_SILENT;
        switch(toupper(*logLevelStr))
        {
            case 'F': case 1: logLevel = LOG_LEVEL_FATAL; break;
            case 'E': case 2: logLevel = LOG_LEVEL_ERROR; break;
            case 'W': case 3: logLevel = LOG_LEVEL_WARNING; break;
            case 'N': case 4: logLevel = LOG_LEVEL_NOTICE; break;
            case 'T': case 5: logLevel = LOG_LEVEL_TRACE; break;
            case 'V': case 6: logLevel = LOG_LEVEL_VERBOSE; break;
        }
        // Store result
        bool logLevelChanged = (_loggingThreshold != logLevel);
        _loggingThreshold = logLevel;
        // Persist if changed
        if (logLevelChanged)
        {
            if (_pConfigBase)
            {
                _pConfigBase->setConfigData(formConfigStr().c_str());
                _pConfigBase->writeConfig();
            }
            if (_logToSerial && _serialPort == 0)
                Serial.printf("NetLog: LogLevel set to %d\n", _loggingThreshold);
        }
        else
        {
            if (_logToSerial && _serialPort == 0)
                Serial.printf("NetLog: LogLevel unchanged at %d\n", _loggingThreshold);
        }
    }

    void setMQTT(bool mqttFlag, const char* mqttLogTopic)
    {
        // Set values
        bool dataChanged = ((_logToMQTT != mqttFlag) || (_mqttLogTopic != mqttLogTopic));
        _logToMQTT = mqttFlag;
        _mqttLogTopic = mqttLogTopic;
        // Persist if changed
        if (dataChanged)
        {
            if (_pConfigBase)
            {
                _pConfigBase->setConfigData(formConfigStr().c_str());
                _pConfigBase->writeConfig();
            }
        }
    }

    void setSerial(bool onOffFlag, const char* serialPortStr)
    {
        // Set values
        bool dataChanged = ((_logToSerial != onOffFlag) || (String(_serialPort) != String(serialPortStr)));
        _logToSerial = onOffFlag;
        _serialPort = atoi(serialPortStr);
        // Persist if changed
        if (dataChanged)
        {
            if (_pConfigBase)
            {
                _pConfigBase->setConfigData(formConfigStr().c_str());
                _pConfigBase->writeConfig();
            }
        }
    }

    void setCmdSerial(bool onOffFlag)
    {
        // Set values
        bool dataChanged = (_logToCommandSerial != onOffFlag);
        _logToCommandSerial = onOffFlag;
        // Persist if changed
        if (dataChanged)
        {
            if (_pConfigBase)
            {
                _pConfigBase->setConfigData(formConfigStr().c_str());
                _pConfigBase->writeConfig();
            }
        }
    }

    void setHTTP(bool httpFlag, const char* ipAddr, const char* portStr, const char* httpLogUrl)
    {
        // Set values
        String ipAddrValidated = ipAddr;
        if (ipAddrValidated.length() == 0)
            ipAddrValidated = _httpIpAddr;
        int portValidated = String(portStr).toInt();
        if (strlen(portStr) == 0)
            portValidated = _httpPort;
        String httpLogUrlValidated = httpLogUrl;
        if (httpLogUrlValidated.length() == 0)
            httpLogUrlValidated = _httpLogUrl;
        bool dataChanged = ((_logToHTTP != httpFlag) || (_httpLogUrl != httpLogUrlValidated) ||
                    (_httpIpAddr != ipAddrValidated) || (_httpPort != portValidated));
        _logToHTTP = httpFlag;
        _httpIpAddr = ipAddrValidated;
        _httpPort = portValidated;
        _httpLogUrl = httpLogUrlValidated;
        // Persist if changed
        if (dataChanged)
        {
            if (_pConfigBase)
            {
                _pConfigBase->setConfigData(formConfigStr().c_str());
                _pConfigBase->writeConfig();
            }
        }
        else
        {
            if (_logToSerial && _serialPort == 0)
                Serial.printf("NetLog: Config data unchanged\n");
        }
    }

    void setup(ConfigBase *pConfig, const char* systemName)
    {
        _systemName = systemName;
        _pConfigBase = pConfig;
        if (!pConfig)
            return;
        if (_logToSerial && _serialPort == 0)
            Serial.printf("NetLog: Setup from %s\n", pConfig->getConfigData());
        // Get the log level
        _loggingThreshold = pConfig->getLong("LogLevel", LOG_LEVEL_SILENT);
        // Get MQTT settings
        _logToMQTT = pConfig->getLong("MQTTFlag", 0) != 0;
        _mqttLogTopic = pConfig->getString("MQTTTopic", "");
        // Get HTTP settings
        _logToHTTP = pConfig->getLong("HTTPFlag", 0) != 0;
        _httpIpAddr = pConfig->getString("HTTPAddr", "");
        _httpPort = pConfig->getLong("HTTPPort", 5076);
        _httpLogUrl = pConfig->getString("HTTPUrl", "");
        // Get Serial settings
        _logToSerial = pConfig->getLong("SerialFlag", 1) != 0;
        _serialPort = pConfig->getLong("SerialPort", 0);
        // Get CommandSerial settings
        _logToCommandSerial = pConfig->getLong("CmdSerial", 0) != 0;
        
        // Debug
        if (_logToSerial && _serialPort == 0)
            Serial.printf("NetLog: logLevel %d, mqttFlag %d topic %s, httpFlag %d, ip %s, port %d, url %s, serialFlag %d, serialPort %d, cmdSerial %d\n",
                    _loggingThreshold, _logToMQTT, _mqttLogTopic.c_str(),
                    _logToHTTP, _httpIpAddr.c_str(), _httpPort, _httpLogUrl.c_str(),
                    _logToSerial, _serialPort, _logToCommandSerial);
    }

    String formConfigStr()
    {
        // This string is stored in NV ram for configuration on power up
        return "{\"LogLevel\":\"" + String(_loggingThreshold) +
                        "\",\"MQTTFlag\":\"" + String(_logToMQTT ? 1 : 0) + 
                        "\",\"MQTTTopic\":\"" + _mqttLogTopic +
                        "\",\"HTTPFlag\":\"" + String(_logToHTTP ? 1 : 0) + 
                        "\",\"HTTPAddr\":\"" + _httpIpAddr + 
                        "\",\"HTTPPort\":\"" + String(_httpPort) + 
                        "\",\"HTTPUrl\":\"" + _httpLogUrl +
                        "\",\"SerialFlag\":\"" + _logToSerial + 
                        "\",\"SerialPort\":\"" + String(_serialPort) + 
                        "\",\"CmdSerial\":\"" + String(_logToCommandSerial) + 
                        "\"}";
    }

    void pause()
    {
        _isPaused = true;
        _pauseStartedMs = millis();
    }

    void resume()
    {
        if (_isPaused)
        {
            _isPaused = false;
            handleLoggedDuringPause();
        }
    }

    size_t write(uint8_t ch)
    {
        int retVal = 0;
        // Check if paused
        if (_isPaused)
        {
            // Check if we can put into the circular buffer
            if ((_pChBuffer != NULL) && _chBufferPosn.canPut())
            {
                _pChBuffer[_chBufferPosn.posToPut()] = ch;
                _chBufferPosn.hasPut();
            }
            // Serial.printf("<LEN%d>",_chBufferPosn.count());
            return retVal;
        }

        // Check for log to serial
        if (_logToSerial)
        {
            if (_serialPort == 0)
                retVal = Serial.write(ch);
        }

        // Check for log to MQTT or HTTP
        if (!(_logToMQTT || _logToHTTP || _logToCommandSerial))
            return retVal;
        
        // Check for first char on line
        if (_firstChOnLine)
        {
            _firstChOnLine = false;
            // Get msg level from first char in message
            int msgLevel = LOG_LEVEL_SILENT;
            switch(ch)
            {
                case 'F': case 1: msgLevel = LOG_LEVEL_FATAL; break;
                case 'E': case 2: msgLevel = LOG_LEVEL_ERROR; break;
                case 'W': case 3: msgLevel = LOG_LEVEL_WARNING; break;
                case 'N': case 4: msgLevel = LOG_LEVEL_NOTICE; break;
                case 'T': case 5: msgLevel = LOG_LEVEL_TRACE; break;
                case 'V': case 6: msgLevel = LOG_LEVEL_VERBOSE; break;
            }
            if (msgLevel <= _loggingThreshold)
            {
                _collectLineForLog = true;
                _curMsgLogLevel = msgLevel;
                _msgToLog = (char)ch;
            }
        }
        else if (_collectLineForLog)
        {
            if (_msgToLog.length() < LOG_LINE_MAXLEN)
                _msgToLog += (char)ch;
        }

        // Check for EOL
        if (ch == '\n')
        {
            _firstChOnLine = true;
            if (_collectLineForLog)
            {
                if (_msgToLog.length() > 0)
                {
                    // Remove linefeeds
                    _msgToLog.replace("\n","");
                    _msgToLog.replace("\r","");
                    if (_logToMQTT || _logToCommandSerial)
                    {
                        String logStr = "{\"logLevel\":" + String(_curMsgLogLevel) + ",\"logMsg\":\"" + String(_msgToLog.c_str()) + "\"}";
                        logStr.replace("\n","");
                        if (_logToMQTT)
                            _mqttManager.reportSilent(logStr.c_str());
                        if (_logToCommandSerial)
                            _commandSerial.logMessage(logStr);
                    }
                    if (_logToHTTP)
                    {
                        // Abandon any existing connection
                        if (_wifiClient.connected())
                        {
                            _wifiClient.stop();
                            // Serial.println("NetLog: Stopped existing TCP conn");
                        }

                        // Connect
                        // Serial.printf("NetLog: TCP conn to %s:%d\n", _httpIpAddr.c_str(), _httpPort);
                        bool connOk = _wifiClient.connect(_httpIpAddr.c_str(), _httpPort);
                        // Serial.printf("NetLog: TCP connect rslt %s\n", connOk ? "OK" : "FAIL");
                        if (connOk)
                        {
                            String logStr = "[{\"logCat\":" + String(_curMsgLogLevel) + ",\"eventText\":\"" + _msgToLog + "\"}]\r\n";
                            static const char* headers = "Content-Type: application/json\r\nAccept: application/json\r\n"
                                        "Host: NetLogger\r\nConnection: close\r\n\r\n";
                            String reqStr = "POST /" + _httpLogUrl + "/" + _systemName + "/ HTTP/1.1\r\nContent-Length:" + String(logStr.length()) + "\r\n";
                            _wifiClient.print(reqStr + headers + logStr);
                        }
                        else
                        {
                            if (_logToSerial && _serialPort == 0)
                                Serial.printf("NetLog: Couldn't connect to %s:%d\n", _httpIpAddr.c_str(), _httpPort);
                        }
                    }
                }
                _msgToLog = "";
            }
            _collectLineForLog = false;
        }
        return retVal;
    }

    void service(char xonXoffChar = 0)
    {
        // Handle WiFi connected - pump any data
        if (_wifiClient.connected())
        {
            // Check for data available on TCP socket
            int numAvail = _wifiClient.available();
            int numToRead = numAvail;
            if (numAvail > MAX_RX_BUFFER_SIZE)
            {
                numToRead = MAX_RX_BUFFER_SIZE;
            }
            if (numToRead > 0)
            {
                uint8_t rxBuf[MAX_RX_BUFFER_SIZE];
                int numRead = _wifiClient.read(rxBuf, numToRead);
                Log.verbose("OTAUpdate: wifiClient reading %d available %d read %d\n", numToRead, numAvail, numRead);
                // Currently just discard received data on the TCP socket
            }
        }

        // Check for busy indicator
        if (xonXoffChar == ASCII_XOFF)
        {
            // Serial.printf("<Logging paused>");
            pause();
        }
        else if (xonXoffChar == ASCII_XON)
        {
            // Serial.printf("<Logging resumed>");
            resume();
        }

        // Check for pause timeout
        if (_isPaused && Utils::isTimeout(millis(), _pauseStartedMs, _pauseTimeMs))
        {
            _isPaused = false;
            handleLoggedDuringPause();
        }
    }

private:
    void handleLoggedDuringPause()
    {
        // Empty the circular buffer
        while ((_pChBuffer != NULL) && _chBufferPosn.canGet())
        {
            write(_pChBuffer[_chBufferPosn.posToGet()]);
            _chBufferPosn.hasGot();
        }
    }
};