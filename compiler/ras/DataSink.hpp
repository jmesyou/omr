
#ifndef OMR_DATASINK_INCL
#define OMR_DATASINK_INCL

#include <string>
#include <memory>
#include "Debug.hpp"

class DataSink {
  public:
    virtual void write(uint8_t byte) = 0;
    virtual void write(std::string string) = 0;
    static std::unique_ptr<DataSink> getSink();
};

class EmptySink : public DataSink {
  public:
    virtual void write(uint8_t byte) override {
      return;
    }

    virtual void write(std::string string) override {
      return;
    }
};

class FileSink : DataSink {

};

class StreamSink : DataSink {

};

#endif
