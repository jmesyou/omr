#include "GraphWriter.hpp"
#include "XMLGraphWriter.hpp"
#include "compiler/control/OMROptions.hpp"
#include "compile/Compilation.hpp"
#include "compile/Compilation_inlines.hpp"
#include <atomic>

std::atomic<int32_t> GraphWriter::nextAvailableCompilationId(0);

GraphWriter * GraphWriter::getGraphWriter(int32_t id, TR_ResolvedMethod * method, TR::Options & options) {
  if (options.getOption(TR_VisualizeTrees)) {
    return new XMLGraphWriter(id, method, options);
  }
  return new DefaultGraphWriter();
}
