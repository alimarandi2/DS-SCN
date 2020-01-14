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

#include "supernode-ds.hpp"
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

#include <ndn-cxx/lp/tags.hpp>

NS_LOG_COMPONENT_DEFINE("Supernode");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Supernode);

TypeId
Supernode::GetTypeId(void)
{
  static TypeId tid =
    TypeId("ns3::ndn::Supernode")
      .SetGroupName("Ndn")
      .SetParent<Consumer>()
      .AddConstructor<Supernode>()

      .AddAttribute("Frequency", "Frequency of interest packets", StringValue("0.1"),
                    MakeDoubleAccessor(&Supernode::m_frequency), MakeDoubleChecker<double>())

      .AddAttribute("Randomize",
                    "Type of send time randomization: none (default), uniform, exponential",
                    StringValue("none"),
                    MakeStringAccessor(&Supernode::SetRandomize, &Supernode::GetRandomize),
                    MakeStringChecker())

      .AddAttribute("MaxSeq", "Maximum sequence number to request",
                    IntegerValue(std::numeric_limits<uint32_t>::max()),
                    MakeIntegerAccessor(&Supernode::m_seqMax), MakeIntegerChecker<uint32_t>())

    ;

  return tid;
}

Supernode::Supernode()
  : m_frequency(0.1)
  , m_firstTime(true)
  , domainFilter(PEC, FPP, UNIVERSAL_SEED) 
{
  m_seqMax = std::numeric_limits<uint32_t>::max();
  m_interestName = ndn::Name("ndn:/localhop/IIM");
}

Supernode::~Supernode()
{
}

void
Supernode::ScheduleNextPacket()
{
  // double mean = 8.0 * m_payloadSize / m_desiredRate.GetBitRate ();
  // std::cout << "next: " << Simulator::Now().ToDouble(Time::S) + mean << "s\n";

  if (m_firstTime) {
    m_sendEvent = Simulator::Schedule(Seconds(0.0), &Supernode::SendPacket, this);
    m_firstTime = false;
  }
  else if (!m_sendEvent.IsRunning())
    m_sendEvent = Simulator::Schedule((m_random == 0) ? Seconds(1.0 / m_frequency)
                                                      : Seconds(m_random->GetValue()),
                                      &Supernode::SendPacket, this);
}

void
Supernode::SendPacket()
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
  uint32_t rand = m_rand->GetValue(0, std::numeric_limits<uint32_t>::max());
  nameWithSequence->appendSequenceNumber(rand);

  // shared_ptr<Interest> interest = make_shared<Interest> ();
  shared_ptr<Interest> interest = make_shared<Interest>();
  interest->setNonce(rand);
  interest->setName(*nameWithSequence);
  // time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
  // interest->setInterestLifetime(interestLifeTime);

  interest->setBfComponents(domainFilter.size(), domainFilter.table(), domainFilter.element_count(), domainFilter.salt_count());

  NS_LOG_INFO("Sending IIM");
  
  WillSendOutInterest(seq);

  m_transmittedInterests(interest, this, m_face);
  m_appLink->onReceiveInterest(*interest);

  ScheduleNextPacket();
}


void
Supernode::SetRandomize(const std::string& value)
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
Supernode::GetRandomize() const
{
  return m_randomType;
}


void
Supernode::OnData(shared_ptr<const Data> data)
{
  if (!m_active)
    return;

  App::OnData(data); // tracing inside

  uint32_t seq = data->getName().at(-1).toSequenceNumber();
  if (data->hasBf()) {
    NS_LOG_INFO("Bloom filter received from " << data->getNodeId());
    bloom_filter filter = data->getBf();
    domainFilter |= filter;
  }
  else {
    NS_LOG_INFO("DATA for sequence number " << seq);
  }

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
}

void
Supernode::OnNack(shared_ptr<const lp::Nack> nack)
{
  App::OnNack(nack);
  NS_LOG_INFO("No service provider in this domain");
}

void
Supernode::StartApplication() // Called at time specified by Start
{
  NS_LOG_FUNCTION_NOARGS();

  // do base stuff
  App::StartApplication();

  ScheduleNextPacket();
}

} // namespace ndn
} // namespace ns3
