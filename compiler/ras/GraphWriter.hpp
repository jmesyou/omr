
#ifndef OMR_GRAPHWRITER_INCL
#define OMR_GRAPHWRITER_INCL

#include <atomic>
#include <vector>
#include <stdexcept>
#include "Debug.hpp"
#include "DataSink.hpp"
#include "env/FilePointerDecl.hpp"
#include "env/OMRIO.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"

template<typename ... Args>
inline std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    char buf[size];
    std::snprintf( buf, size, format.c_str(), args ... );
    return std::string(buf); // We don't want the '\0' inside
}

#pragma GCC diagnostic pop

class TR_BlockStructure;
class TR_RegionStructure;
namespace TR {
  class Compilation;
  class Block;
  class TreeTop;
  class Optimizer;
  class Node;
  class ResolvedMethodSymbol;
  class AllBlockIterator;
}

struct InputEdgeInfo {
  bool isIndirect;
  std::string name;
  int32_t type;
};

struct OutputEdgeInfo {
  bool isIndirect;
  std::string name;
};

struct StringProperty {
  std::string key;
  std::string value;
};

struct IntegerProperty {
  std::string name;
  int32_t     value;
};

struct Properties {
  std::vector<IntegerProperty> integers;
  std::vector<StringProperty>  strings;

  Properties add(StringProperty && property) {
    strings.emplace_back(property);
    return *this;
  }

  Properties add(IntegerProperty && property) {
    integers.emplace_back(property);
    return *this;
  }
};

class GraphWriter {
    public:
      virtual void writeGraph(std::string & title, TR::Compilation * compilation, TR::ResolvedMethodSymbol * methodSymbol) = 0;
      virtual void complete() = 0;
      static GraphWriter * getGraphWriter(int32_t id, TR_ResolvedMethod * method, TR::Options & options);
      virtual ~GraphWriter() = default;

    protected:
    static int32_t getNextAvailableCompilationId() {
      int32_t id = nextAvailableCompilationId.fetch_add(1);
      return id;
    }

    private:
    static std::atomic<int32_t> nextAvailableCompilationId;
};

class DefaultGraphWriter : public GraphWriter {
  public:
    void writeGraph(std::string & title, TR::Compilation * compilation, TR::ResolvedMethodSymbol * methodSymbol) override {
      return;
    }

    void complete() override {}
};

#endif
