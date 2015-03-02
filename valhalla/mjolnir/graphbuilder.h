#ifndef VALHALLA_MJOLNIR_GRAPHBUILDER_H
#define VALHALLA_MJOLNIR_GRAPHBUILDER_H

#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <utility>
#include <algorithm>
#include <memory>
#include <boost/property_tree/ptree.hpp>

#include <valhalla/baldr/tilehierarchy.h>
#include <valhalla/baldr/graphid.h>
#include <valhalla/baldr/signinfo.h>

#include <valhalla/mjolnir/osmdata.h>
#include <valhalla/mjolnir/osmnode.h>
#include <valhalla/mjolnir/osmway.h>
#include <valhalla/mjolnir/sequence.h>
#include <valhalla/mjolnir/dataquality.h>
#include <valhalla/mjolnir/edgeinfobuilder.h>

namespace valhalla {
namespace mjolnir {

/**
 * An edge in the graph. Connects 2 nodes that have 2 or more "uses" - meaning
 * the node forms an intersection (or is the end of an OSM way). OSM nodes
 * with less than 2 uses become a shape point (lat,lng) along the edge.
 */
struct Edge {
  // GraphId of the source (start) node of the edge
  baldr::GraphId sourcenode_;

  // Index into the list of OSM way information
  uint32_t wayindex_;

  // Index of the first lat,lng into the GraphBuilder latlngs
  uint32_t llindex_;

  // Attributes needed to sort the edges
  struct EdgeAttributes {
    uint32_t llcount          : 16;
    uint32_t importance       : 3;
    uint32_t driveableforward : 1;
    uint32_t driveablereverse : 1;
    uint32_t traffic_signal   : 1;
    uint32_t forward_signal   : 1;
    uint32_t backward_signal  : 1;
    uint32_t link             : 1;
    uint32_t spare            : 7;
  };
  EdgeAttributes attributes;

  // GraphId of the target (end) node of the edge
  baldr::GraphId targetnode_;


  /**
   * Construct a new edge. Target node and additional lat,lngs will
   * be filled in later.
   * @param sourcenode   Start node of the edge
   * @param wayindex     Index into list of OSM ways
   * @param ll           Lat,lng at the start of the edge.
   */
  static Edge make_edge(const baldr::GraphId& sourcenode, const uint32_t wayindex,
       const uint32_t llindex, const OSMWay& way) {
    Edge e{sourcenode, wayindex, llindex};
    e.attributes.llcount = 1;
    e.attributes.importance = static_cast<uint32_t>(way.road_class());
    e.attributes.driveableforward = way.auto_forward();
    e.attributes.driveablereverse = way.auto_backward();
    e.attributes.link = way.link();
    return e;
  }

};

/**
 * Node within the graph
 */
struct Node {
  // List of edges connected to the node
  std::list<uint32_t> edges;

  // Node attributes
  NodeAttributes attributes;

  /**
   * Constructor.
   */
  Node()
      : attributes{} {
  }

  /**
   * Constructor with arguments
   */
  Node(const NodeAttributes& attr, const uint32_t edgeindex, const bool link)
      : attributes(attr) {
    AddEdge(edgeindex, link);
  }

  /**
   * Add an edge. Set flags to indicate a link and/or non-link edge
   * exists at the node.
   * @param  edgeindex  Index in the list of edges.
   * @param  link       Flag indicating whether this edge is a link
   *                    (highway=*_link)
   */
  void AddEdge(const uint32_t edgeindex, const bool link) {
    if (link) {
      attributes.link_edge = true;
    } else {
      attributes.non_link_edge = true;
    }

    // TODO - could insert into the list based on importance and
    // driveablity?
    edges.push_back(edgeindex);
  }

  /**
   * Get the number of edges beginning or ending at the node.
   * @return  Returns the number of edges.
   */
  uint32_t edge_count() const {
    return edges.size();
  }

  /**
   * Set the exit to flag
   */
  void set_exit_to(const bool exit_to) {
    attributes.exit_to = exit_to;
  }

  /**
   * Get the exit to flag
   */
  bool exit_to() const {
    return attributes.exit_to;
  }

  /**
   * Set the ref flag
   */
  void set_ref(const bool ref)  {
    attributes.ref = ref;
  }

  /**
   * Get the ref flag
   */
  bool ref() const {
    return attributes.ref;
  }

  /**
   * Get the name flag
   */
  bool name() const  {
    return attributes.name;
  }

  /**
   * Set access mask.
   */
  void set_access_mask(const uint32_t access_mask) {
    attributes.access_mask = access_mask;
  }

  /**
   * Get the access mask.
   */
  uint32_t access_mask() const {
    return attributes.access_mask;
  }

  /**
   * Set the node type.
   */
  void set_type(const NodeType type) {
    attributes.type = static_cast<uint8_t>(type);
  }

  /**
   * Get the node type.
   */
  NodeType type() const {
    return static_cast<baldr::NodeType>(attributes.type);
  }

  /**
   * Set traffic_signal flag.
   */
  void set_traffic_signal(const bool traffic_signal) {
    attributes.traffic_signal = traffic_signal;
  }

  /**
   * Get the traffic_signal flag.
   */
  bool traffic_signal() const {
    return attributes.traffic_signal;
  }

  /**
   * Get the non-link edge flag. True if any connected edge is not a
   * highway=*_link.
   */
  bool non_link_edge() const {
    return attributes.non_link_edge;
  }

  /**
   * Get the non-link edge flag. True if any connected edge is a
   * highway=*_link.
   */
  bool link_edge() const  {
    return attributes.link_edge;
  }
};

/**
 * Class used to construct temporary data used to build the initial graph.
 */
class GraphBuilder {
 public:
  //not default constructable or copyable
  GraphBuilder() = delete;
  GraphBuilder(const GraphBuilder&) = delete;

  /**
   * Constructor
   */
  GraphBuilder(const boost::property_tree::ptree& pt);

  /**
   * Tell the builder to build the tiles from the provided datasource
   * and configs
   * @param  osmdata  OSM data used to build the graph.
   */
  void Build(OSMData& osmdata);

 protected:

  /**
   * Construct edges in the graph.
   * @param  osmdata  OSM data used to construct edges in the graph.
   * @param  tilesize Tile size in degrees.
   */
  void ConstructEdges(const OSMData& osmdata, const float tilesize);

  /**
   * Add a new node to the tile (based on the OSM node lat,lng). Return
   * the GraphId of the node.
   */
  GraphId AddNodeToTile(const uint64_t osmnodeid, const OSMNode& osmnode,
                        const uint32_t edgeindex, const bool link);

  /**
   * Get a reference to a node given its graph Id.
   * @param  id  GraphId of the node.
   * @return  Returns a const reference to the node information.
   */
  Node& GetNode(const baldr::GraphId& id);

  /**
   * Update road class / importance of links (ramps)
   */
  void ReclassifyLinks(const std::string& ways_file);

  /**
   * Get the best classification for any non-link edges from a node.
   * @param  node  Node - gets outbound edges from this node.
   * @param  edges The file backed list of edges in the graph.
   * @return  Returns the best (most important) classification
   */
  uint32_t GetBestNonLinkClass(const Node& node, sequence<Edge>& edges) const;

  /**
   * Build tiles representing the local graph
   */
  void BuildLocalTiles(const uint8_t level, const OSMData& osmdata) const;

  static std::string GetRef(const std::string& way_ref,
                            const std::string& relation_ref);

  static std::vector<SignInfo> CreateExitSignInfoList(
      const GraphId& nodeid, const Node& node, const OSMWay& way,
      const OSMData& osmdata,
      const std::unordered_map<baldr::GraphId, std::string>& node_ref,
      const std::unordered_map<baldr::GraphId, std::string>& node_exit_to,
      const std::unordered_map<baldr::GraphId, std::string>& node_name);

  /**
   * Create the extended node information mapped by the node's GraphId.
   * This is needed since we do not keep osmnodeid around.
   * @param  osmdata  OSM data with the extended node information mapped by
   *                  OSM node Id.
   */
  void CreateNodeMaps(const OSMData& osmdata);

  /**
   * Update restrictions. Replace OSM node Ids with GraphIds.
   * @param  osmdata  Includes the restrictions
   */
  void UpdateRestrictions(OSMData& osmdata);

  // List of the tile levels to be created
  uint32_t level_;
  TileHierarchy tile_hierarchy_;

  // Map of OSM node Ids to GraphIds, for sparse objects like exits,
  std::unordered_map<uint64_t, GraphId> nodes_;

  // Map that stores all the reference info on a node
  std::unordered_map<baldr::GraphId, std::string> node_ref_;

  // Map that stores all the exit to info on a node
  std::unordered_map<baldr::GraphId, std::string> node_exit_to_;

  // Map that stores all the name info on a node
  std::unordered_map<baldr::GraphId, std::string> node_name_;

  // Stores all the edges in this file
  std::string edges_file_;

  // A place to keep each tile's nodes so that various threads can
  // write various tiles asynchronously
  std::unordered_map<GraphId, std::vector<Node> > tilednodes_;

  // Data quality / statistics.
  std::unique_ptr<DataQuality> stats_;

  // How many threads to run
  const unsigned int threads_;
};

}
}

#endif  // VALHALLA_MJOLNIR_GRAPHBUILDER_H
