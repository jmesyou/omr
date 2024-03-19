#include "DataSink.hpp"
#include <memory>

std::unique_ptr<DataSink> DataSink::getSink() {
  return std::make_unique<EmptySink>();
}
