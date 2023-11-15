/*******************************************************************************
 * Copyright IBM Corp. and others 2000
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at http://eclipse.org/legal/epl-2.0
 * or the Apache License, Version 2.0 which accompanies this distribution
 * and is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following Secondary
 * Licenses when the conditions for such availability set forth in the
 * Eclipse Public License, v. 2.0 are satisfied: GNU General Public License,
 * version 2 with the GNU Classpath Exception [1] and GNU General Public
 * License, version 2 with the OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#ifndef OMR_BINARYGRAPHWRITER_INCL
#define OMR_BINARYGRAPHWRITER_INCL

#include <map>
#include <vector>
#include "GraphWriter.hpp"

template <class T> class Pool {
  std::map<T, uint16_t> map;

  public:
    bool contains(T elem) {
      return map.count(elem) == 1;
    }

    uint16_t lookup(T elem) {
      return map.at(elem);
    }

    uint16_t insert(T elem, uint16_t id) {
      map.insert({elem, id});
      return id;
    }
};

class BinaryGraphWriter;

class PoolEnumClass {
  public:
    std::string name;
    std::vector<std::string> values;
    PoolEnumClass(std::string _name, std::vector<std::string> _values): name(_name), values(_values) {}
};

class BinaryGraphWriter {
  public:
    BinaryGraphWriter(TR::Compilation * comp);
    ~BinaryGraphWriter();
    TR::Compilation * getCompilation();
    void initialize(TR::ResolvedMethodSymbol * symbol);
    void writeInt8(int8_t byte);
    void writeInt16(int16_t n);
    void writeInt32(int32_t n);
    void writeString(std::string string);

    void writeGraph(char * title, TR::ResolvedMethodSymbol * methodSymbol);
    void writeProperties(std::vector<StringProperty> & properties);
    void writeStringProperty(StringProperty & property);
    void writePropString(std::string string);
    void writePoolString(std::string string);
    void writePoolReference(int8_t type, uint16_t id);
    void writePoolNodeClass(TR::Node * node, std::vector<InputEdgeInfo> && inputs, std::vector<OutputEdgeInfo> && outputs);

    void writePoolMethod(TR::ResolvedMethodSymbol * symbol);
    void writePoolClass(std::string name);
    void writePoolEnumClass(PoolEnumClass cls);
    void writePoolEnumValue(uint32_t index, PoolEnumClass cls);
    void writePoolSignature(TR::ResolvedMethodSymbol * symbol);
    void writeDirectEdge(int32_t node_id);
    void writeInputEdgeInfo(InputEdgeInfo info);
    void writeOutputEdgeInfo(OutputEdgeInfo info);

  private:
    TR::Compilation * _comp;
    std::vector<int8_t> buffer;
    Pool<std::string> stringPool;
    Pool<std::tuple<std::string, int, int>> nodeClassPool;
    Pool<std::string> methodPool;
    Pool<std::string> signaturePool;
    Pool<std::string> typePool;
    Pool<std::tuple<std::string, int>> enumValuePool;
    uint16_t poolId;


    ::FILE * _file;
    bool initialized;

    void flushBuffer(bool force);
    uint16_t getNextPoolId();
};

#endif
