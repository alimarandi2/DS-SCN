# Bloom Filter-based Routing for Dominating Set-based Service-Centric Networks


## Abstract

A service-centric network requires a routing protocol that routes service requests
towards service providers. Routing operations can be divided into intra-domain and
inter-domain routing. In the proposed approach, a so-called supernode is responsible
for managing its own domain as well as for communicating with the supernodes of
other domains to perform inter-domain routing. To prepare routing information, the
nodes of each domain inform their supernodes about their available service names
and resources (e.g., CPU, RAM). To this aim, the nodes use Bloom filters, which
reduce bandwidth and storage overhead. In order to appoint appropriate nodes as
supernodes in the network topology, in this thesis, we use Dominating Sets (DS) and
Connected Dominating Sets (CDS).

A DS is a subset of a graph, where each element of the graph is either in the subset
or directly adjacent to an element of the subset. A CDS is a DS, where all elements of
the subset are connected. We propose fully distributed algorithms for constructing
DS as well as CDS over the network topology.

#### Usage

For the clustering to work, the Clusterconsumer and Clusterproducer apps need to be installed on every node in any given network.

#### Repository

This repository contains the code of the ndnSIM-apps responsible for the clustering algorithm. The project uses ndnSIM 2.5.0.

## ndnSIM

Based on [ndnSIM](http://ndnsim.net/current/index.html) / [ndnSIM on github](https://github.com/named-data-ndnSIM/ndnSIM)

ndnSIM is licensed under conditions of GNU General Public License version 3.0
