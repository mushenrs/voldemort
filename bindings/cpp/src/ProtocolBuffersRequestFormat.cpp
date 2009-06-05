/* -*- C++ -*-; c-basic-offset: 4; indent-tabs-mode: nil */
/*
 * Implementation for ProtocolBuffersRequestFormat class.
 * 
 * Copyright (c) 2009 Webroot Software, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

#include <utility>
#include <vector>
#include <voldemort/VoldemortException.h>
#include "ProtocolBuffersRequestFormat.h"
#include "voldemort-client.pb.h"
#include <arpa/inet.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace Voldemort {

using namespace google::protobuf::io;

#define READ_INT(inputStream, val)              \
    inputStream->read((char*)&val, 4);          \
    val = ntohl(val);

ProtocolBuffersRequestFormat::ProtocolBuffersRequestFormat() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
}

ProtocolBuffersRequestFormat::~ProtocolBuffersRequestFormat() {

}

static void setupVectorClock(voldemort::VectorClock* vvc, VectorClock* vc) {
    std::list<std::pair<short, uint64_t> >::const_iterator it;
    std::list<std::pair<short, uint64_t> >* entries = vc->getEntries();

    for (it = entries->begin(); it != entries->end(); ++it) {
        voldemort::ClockEntry* entry = vvc->add_entries();
        entry->set_node_id((*it).first);
        entry->set_version((*it).second);
    }
}

static VectorClock* readVectorClock(const voldemort::VectorClock* vvc) {
    std::list<std::pair<short, uint64_t> > entries;
    for (int i = 0; i < vvc->entries_size(); i++) {
        entries.push_back(std::make_pair((short)vvc->entries(i).node_id(),
                                         (uint64_t)vvc->entries(i).version()));
    }
    return new VectorClock(&entries, (uint64_t)vvc->timestamp());
}

static void throwException(const voldemort::Error& error) {
    /* XXX - TODO map error types to various exception objects derived
       from VoldemortException */
    throw VoldemortException(error.error_message());
}

static void writeMessageWithLength(std::ostream* outputStream,
                                   google::protobuf::Message* message) {
    uint32_t mLen = htonl(message->ByteSize());
    outputStream->write((char*)&mLen, 4);
    message->SerializeToOstream(outputStream);
}

static void readMessageWithLength(std::istream* inputStream,
                                  google::protobuf::Message* message) {
    uint32_t mLen;
    READ_INT(inputStream, mLen);
    
    std::vector<char> buffer(mLen);
    inputStream->read(&buffer[0], mLen);

    message->ParseFromArray((void*)&buffer[0], mLen);
}

void ProtocolBuffersRequestFormat::writeGetRequest(std::ostream* outputStream,
                                                   const std::string* storeName,
                                                   const std::string* key,
                                                   bool shouldReroute) {
    voldemort::VoldemortRequest req;
    req.set_type(voldemort::GET);
    req.set_store(*storeName);
    req.set_should_route(shouldReroute);
    req.mutable_get()->set_key(*key);

    writeMessageWithLength(outputStream, &req);
}

std::list<VersionedValue> * 
ProtocolBuffersRequestFormat::readGetResponse(std::istream* inputStream) {
    voldemort::GetResponse res;
    readMessageWithLength(inputStream, &res);
    if (res.has_error()) {
        throwException(res.error());
    }

    std::list<VersionedValue>* responseList = NULL;
    VectorClock* clock = NULL;
    std::string* val = NULL;

    try {
        responseList = new std::list<VersionedValue>();
        for (int i = 0; i < res.versioned_size(); i++) {
            val = new std::string(res.versioned(i).value());
            clock = readVectorClock(&res.versioned(i).version());
            VersionedValue vv(val, clock);
            val = NULL;
            clock = NULL;
            responseList->push_back(vv);
        }
    } catch (...) {
        if (responseList) delete responseList;
        if (clock) delete clock;
        if (val) delete val;
        throw;
    }

    return responseList;
}

void ProtocolBuffersRequestFormat::writeGetAllRequest(std::ostream* outputStream,
                                                      const std::string* storeName,
                                                      std::list<const std::string*>* keys,
                                                      bool shouldReroute) {
    throw "not implemented";
}

void ProtocolBuffersRequestFormat::writePutRequest(std::ostream* outputStream,
                                                   const std::string* storeName,
                                                   const std::string* key,
                                                   const std::string* value,
                                                   VectorClock* version,
                                                   bool shouldReroute) {
    voldemort::VoldemortRequest req;
    req.set_type(voldemort::PUT);
    req.set_store(*storeName);
    req.set_should_route(shouldReroute);

    voldemort::PutRequest* preq = req.mutable_put();
    preq->set_key(*key);

    voldemort::Versioned* vers = preq->mutable_versioned();
    vers->set_value(*value);
    setupVectorClock(vers->mutable_version(), version);

    writeMessageWithLength(outputStream, &req);
}

void ProtocolBuffersRequestFormat::readPutResponse(std::istream* inputStream) {
    voldemort::PutResponse res;
    readMessageWithLength(inputStream, &res);

    if (res.has_error()) {
        throwException(res.error());
    }
}

void ProtocolBuffersRequestFormat::writeDeleteRequest(std::ostream* outputStream,
                                                      const std::string* storeName,
                                                      const std::string* key,
                                                      VectorClock* version,
                                                      bool shouldReroute) {
    voldemort::VoldemortRequest req;
    req.set_type(voldemort::DELETE);
    req.set_store(*storeName);
    req.set_should_route(shouldReroute);

    voldemort::DeleteRequest* dreq = req.mutable_delete_();
    dreq->set_key(*key);
    setupVectorClock(dreq->mutable_version(), version);

    writeMessageWithLength(outputStream, &req);
}

bool ProtocolBuffersRequestFormat::readDeleteResponse(std::istream* inputStream) {
    voldemort::DeleteResponse res;
    readMessageWithLength(inputStream, &res);

    if (res.has_error()) {
        throwException(res.error());
    }
    return res.success();
}

} /* namespace Voldemort */
