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

#include "clusterc.hpp"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/integer.h"
#include "ns3/double.h"

#include "supernode-ds.hpp"

#include <ndn-cxx/lp/tags.hpp>
#include <stdint.h>

NS_LOG_COMPONENT_DEFINE("Clusterconsumer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Clusterconsumer);

TypeId
Clusterconsumer::GetTypeId(void)
{
  static TypeId tid =
    TypeId("ns3::ndn::Clusterconsumer")
      .SetGroupName("Ndn")
      .SetParent<Consumer>()
      .AddConstructor<Clusterconsumer>()

      .AddAttribute("Frequency", "Frequency of interest packets", StringValue("0.001"),
                    MakeDoubleAccessor(&Clusterconsumer::m_frequency), MakeDoubleChecker<double>())

      .AddAttribute("Randomize",
                    "Type of send time randomization: none (default), uniform, exponential",
                    StringValue("none"),
                    MakeStringAccessor(&Clusterconsumer::SetRandomize, &Clusterconsumer::GetRandomize),
                    MakeStringChecker())

      .AddAttribute("MaxSeq", "Maximum sequence number to request",
                    IntegerValue(std::numeric_limits<uint32_t>::max()),
                    MakeIntegerAccessor(&Clusterconsumer::m_seqMax), MakeIntegerChecker<uint32_t>())

    ;

  return tid;
}

Clusterconsumer::Clusterconsumer()
  : m_frequency(1.0)
  , m_firstTime(true)
  , m_answers(0)
{
  m_seqMax = std::numeric_limits<uint32_t>::max();
}

Clusterconsumer::~Clusterconsumer()
{
}

// Application Methods
void
Clusterconsumer::StartApplication() // Called at time specified by Start
{
  NS_LOG_FUNCTION_NOARGS();

  // do base stuff
  App::StartApplication();

  // Adds own values into neighbouring table
  best_nId = this->GetNode()->GetId();
  best_face = 0;
  best_nN = this->GetNode()->GetNDevices();

  ScheduleNextPacket();
}


void
Clusterconsumer::ScheduleNextPacket()
{
  // double mean = 8.0 * m_payloadSize / m_desiredRate.GetBitRate ();
  // std::cout << "next: " << Simulator::Now().ToDouble(Time::S) + mean << "s\n";

  if (m_firstTime) {
    m_sendEvent = Simulator::Schedule(Seconds(0.0), &Clusterconsumer::SendPacket, this);
    m_firstTime = false;
  }
  else if (!m_sendEvent.IsRunning())
    m_sendEvent = Simulator::Schedule((m_random == 0) ? Seconds(1.0 / m_frequency)
                                                      : Seconds(m_random->GetValue()),
                                      &Clusterconsumer::SendPacket, this);
}

void
Clusterconsumer::SendPacket()
{
  if (!m_active)
    return;

  //NS_LOG_FUNCTION_NOARGS();

  uint32_t seq = std::numeric_limits<uint32_t>::max(); // invalid

  while (m_retxSeqs.size()) {
    seq = *m_retxSeqs.begin();
    m_retxSeqs.erase(m_retxSeqs.begin());
    break;
  }

  if (seq == std::numeric_limits<uint32_t>::max()) {
    if (m_seqMax != std::numeric_limits<uint32_t>::max()) {
      if (m_seq >= m_seqMax) {
        return; // we are totally done
      }
    }

    seq = m_seq++;
  }

  shared_ptr<Name> nameWithSequence = make_shared<Name>(m_interestName);
  nameWithSequence->append("CII");
  nameWithSequence->appendNumber(this->GetNode()->GetId());

  shared_ptr<Interest> interest = make_shared<Interest>();
  interest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
  interest->setName(*nameWithSequence);
  time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
  interest->setInterestLifetime(interestLifeTime);
  interest->setCII();

  NS_LOG_INFO("Sending " << interest->getName());
  
  WillSendOutInterest(seq);

  m_transmittedInterests(interest, this, m_face);
  m_appLink->onReceiveInterest(*interest);

  ScheduleNextPacket();
}


void
Clusterconsumer::SetRandomize(const std::string& value)
{
  if (value == "uniform") {
    m_random = CreateObject<UniformRandomVariable>();
    m_random->SetAttribute("Min", DoubleValue(0.0));
    m_random->SetAttribute("Max", DoubleValue(2 * 1.0 / m_frequency));
  }
  else if (value == "exponential") {
    m_random = CreateObject<ExponentialRandomVariable>();
    m_random->SetAttribute("Mean", DoubleValue(1.0 / m_frequency));
    m_random->SetAttribute("Bound", DoubleValue(50 * 1.0 / m_frequency));
  }
  else
    m_random = 0;

  m_randomType = value;
}

std::string
Clusterconsumer::GetRandomize() const
{
  return m_randomType;
}


void
Clusterconsumer::OnData(shared_ptr<const Data> data)
{
  if (!m_active)
    return;

  App::OnData(data); // tracing inside

  NS_LOG_INFO(data->getName() << " from " << data->getNodeId() << " received");

  uint32_t seq = 0;

  int hopCount = 0;
  auto hopCountTag = data->getTag<lp::HopCountTag>();
  if (hopCountTag != nullptr) { // e.g., packet came from local node's cache
    hopCount = *hopCountTag;
  }

  SeqTimeoutsContainer::iterator entry = m_seqLastDelay.find(seq);
  if (entry != m_seqLastDelay.end()) {
    m_lastRetransmittedInterestDataDelay(this, seq, Simulator::Now() - entry->time, hopCount);
  }

  entry = m_seqFullDelay.find(seq);
  if (entry != m_seqFullDelay.end()) {
    m_firstInterestDataDelay(this, seq, Simulator::Now() - entry->time, m_seqRetxCounts[seq], hopCount);
  }

  m_seqRetxCounts.erase(seq);
  m_seqFullDelay.erase(seq);
  m_seqLastDelay.erase(seq);

  m_seqTimeouts.erase(seq);
  m_retxSeqs.erase(seq);

  m_rtt->AckSeq(SequenceNumber32(seq));

  // When a data with neighbours is received, the data is stored in a list
  if (data->getNeighbours() > 0) {
    m_answers++;
    NEntry nEntry = {data->getNodeId(), data->getFaceId(), data->getNeighbours()};
    m_neighbours.push_back(nEntry);
    if (data->getNeighbours() > best_nN) {
      best_nId = data->getNodeId();
      best_face = data->getFaceId();
      best_nN = data->getNeighbours();
    }
    

    // When there has been an answer from all neighbouring nodes, the neighbour with the highest degree is calculated
    if (m_answers >= this->GetNode()->GetNDevices())
    {
      if (best_face == 0)
      {
        best_face = data->getSCIFace();
        NEntry nEntry = {best_nId, best_face, best_nN};
        m_neighbours.insert(m_neighbours.begin(), nEntry);
      }
      BestNeighbour();
    }
  } 
  if (data->isSCI()) {
    NS_LOG_INFO("Setting node " << data->getNodeId() << " as it's Supernode through face " << data->getFaceId());
    this->GetNode()->SetSupernodeFace(data->getFaceId());
  }
}

void Clusterconsumer::BestNeighbour()
{
  for (std::vector<NEntry>::iterator it = m_neighbours.begin() ; it != m_neighbours.end(); ++it)
    NS_LOG_DEBUG("Node= " << (*it).getNodeId() << ", Face= " << (*it).getFaceId() << ", Neighbours= " << (*it).getNeighbours());

  if (best_nId == this->GetNode()->GetId()) {
    NS_LOG_INFO("This node is best with " << best_nN << " neighbours");

    if (!this->GetNode()->IsSupernode())
    {    
      this->GetNode()->AddApplication(new Supernode);
      this->GetNode()->SetAsSupernode();
      this->GetNode()->SetSupernodeFace(best_face);
    }
  } else {
    NS_LOG_INFO("Best neighbour is Node " << best_nId << " with " << best_nN << " neighbours"); 
    SendSupernode();   
  }
}

void Clusterconsumer::SendSupernode()
{
  uint32_t seq = m_seq++;

  shared_ptr<Name> nameWithSequence = make_shared<Name>("ndn:/localhop/Cluster/SCI");
  nameWithSequence->appendNumber(this->GetNode()->GetId());

  shared_ptr<Interest> interest = make_shared<Interest>();
  interest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
  interest->setName(*nameWithSequence);
  time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
  interest->setInterestLifetime(interestLifeTime);
  interest->setSCI();

  shared_ptr<ndn::lp::NextHopFaceIdTag> tag = make_shared<ndn::lp::NextHopFaceIdTag>(best_face);
  interest->setTag(tag);

  NS_LOG_INFO("Sending " << interest->getName() << " to Node " << best_nId);
  
  WillSendOutInterest(seq);

  m_transmittedInterests(interest, this, m_face);
  m_appLink->onReceiveInterest(*interest);
  return;
}

} // namespace ndn
} // namespace ns3
