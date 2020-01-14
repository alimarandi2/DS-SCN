/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "clusterp.hpp"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"

#include "model/ndn-l3-protocol.hpp"
#include "helper/ndn-fib-helper.hpp"

#include <memory>

#include "supernode-cds.hpp"

NS_LOG_COMPONENT_DEFINE("Clusterproducer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Clusterproducer);

TypeId
Clusterproducer::GetTypeId(void)
{
  static TypeId tid =
    TypeId("ns3::ndn::Clusterproducer")
      .SetGroupName("Ndn")
      .SetParent<App>()
      .AddConstructor<Clusterproducer>()
      .AddAttribute("Prefix", "Prefix, for which Clusterproducer has the data", StringValue("/"),
                    MakeNameAccessor(&Clusterproducer::m_prefix), MakeNameChecker())

      .AddAttribute(
         "Postfix",
         "Postfix that is added to the output data (e.g., for adding Clusterproducer-uniqueness)",
         StringValue("/"), MakeNameAccessor(&Clusterproducer::m_postfix), MakeNameChecker())


      .AddAttribute("PayloadSize", "Virtual payload size for Content packets", UintegerValue(1024),
                    MakeUintegerAccessor(&Clusterproducer::m_virtualPayloadSize),
                    MakeUintegerChecker<uint32_t>())

      .AddAttribute("Freshness", "Freshness of data packets, if 0, then unlimited freshness",
                    TimeValue(Seconds(0)), MakeTimeAccessor(&Clusterproducer::m_freshness),
                    MakeTimeChecker())

      .AddAttribute(
         "Signature",
         "Fake signature, 0 valid signature (default), other values application-specific",
         UintegerValue(0), MakeUintegerAccessor(&Clusterproducer::m_signature),
         MakeUintegerChecker<uint32_t>())

      .AddAttribute("KeyLocator",
                    "Name to be used for key locator.  If root, then key locator is not used",
                    NameValue(), MakeNameAccessor(&Clusterproducer::m_keyLocator), MakeNameChecker());
  return tid;
}

Clusterproducer::Clusterproducer()
{
  //NS_LOG_FUNCTION_NOARGS();
}

// inherited from Application base class.
void
Clusterproducer::StartApplication()
{
  NS_LOG_FUNCTION_NOARGS();
  App::StartApplication();

  FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
}

void
Clusterproducer::StopApplication()
{
  NS_LOG_FUNCTION_NOARGS();

  App::StopApplication();
}

void
Clusterproducer::OnInterest(shared_ptr<const Interest> interest)
{
  App::OnInterest(interest); // tracing inside
  
  Name dataName(interest->getName());
  // dataName.append(m_postfix);
  // dataName.appendVersion();

  if (!m_active)
    return;

  auto data = make_shared<Data>();
  data->setName(dataName);
  data->setFreshnessPeriod(::ndn::time::milliseconds(m_freshness.GetMilliSeconds()));

  data->setContent(make_shared< ::ndn::Buffer>(m_virtualPayloadSize));

  Signature signature;
  SignatureInfo signatureInfo(static_cast< ::ndn::tlv::SignatureTypeValue>(255));

  if (m_keyLocator.size() > 0) {
    signatureInfo.setKeyLocator(m_keyLocator);
  }

  signature.setInfo(signatureInfo);
  signature.setValue(::ndn::makeNonNegativeIntegerBlock(::ndn::tlv::SignatureValue, m_signature));
  data->setSignature(signature);
  

  NS_LOG_INFO(interest->getName() << " received");
  data->setNodeId(this->GetNode()->GetId());
  if(interest->isCII())
  {
    data->setNeighbours(this->GetNode()->GetNDevices());
  } 
  else if (interest->isSCI())
  {
    data->setSCI();
    uint32_t nApp = this->GetNode()->GetNApplications();
    bool isSupernode = false;
    for (uint32_t i = 0; i<nApp; i++) {
      if (this->GetNode()->IsSupernode()) {
        isSupernode = true;
      }
    }
    if (isSupernode) {
      NS_LOG_INFO("Already a Supernode");
    } else {         
      this->GetNode()->AddApplication(new SupernodeCDS);
      NS_LOG_INFO("Transforming into Supernode");
      this->GetNode()->SetAsSupernode();
      this->GetNode()->SetSupernodeFace(interest->getSCIFace());
    }
  } else { return; }


  // to create real wire encoding
  data->wireEncode();

  m_transmittedDatas(data, this, m_face);
  m_appLink->onReceiveData(*data);
}

} // namespace ndn
} // namespace ns3
