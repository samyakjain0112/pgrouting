/*PGR-GNU*****************************************************************

Copyright (c) 2015 pgRouting developers
Mail: project@pgrouting.org

Copyright (c) 2016 Andrea Nardelli
Mail: nrd.nardelli@gmail.com

------

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

********************************************************************PGR-GNU*/

#include "pgr_maxflow.hpp"

namespace pgrouting {
namespace graph {


PgrFlowGraph::PgrFlowGraph(const std::vector<pgr_edge_t> &edges,
        const std::set<int64_t> &source_vertices,
        const std::set<int64_t> &sink_vertices,
        int algorithm) {
    /* In multi source flow graphs, a super source is created connected to all sources with "infinite" capacity
     * The same applies for sinks.
     * To avoid code repetition, a supersource/sink is used even in the one to one signature.
     */

    /*
     * vertices = {sources} U {sink} U {edges.source} U {edge.target}
     */
    std::set<int64_t> vertices(source_vertices);
    vertices.insert(sink_vertices.begin(), sink_vertices.end());

    for (const auto e : edges) {
        vertices.insert(e.source);
        vertices.insert(e.target);
    }

    for (const auto id : vertices) {
        V v = add_vertex(graph);
        id_to_V.insert(std::pair<int64_t, V>(id, v));
        V_to_id.insert(std::pair<V, int64_t>(v, id));
    }

    set_supersource(source_vertices);
    set_supersink(sink_vertices);

    capacity = get(boost::edge_capacity, graph);
    rev = get(boost::edge_reverse, graph);
    residual_capacity = get(boost::edge_residual_capacity, graph);

    /* Inserting edges
     * Push-relabel requires each edge to be mapped to its reverse with capacity 0.
     * The other algorithms have no such requirement. (can have have as many edges)
     */
    if (algorithm == 1) {
        insert_edges_push_relabel(edges);
    } else {
        insert_edges(edges);
    }

}

void PgrFlowGraph::insert_edges_push_relabel(
        const std::vector<pgr_edge_t> &edges) {
    bool added;
    for (const auto edge : edges) {
        V v1 = get_boost_vertex(edge.source);
        V v2 = get_boost_vertex(edge.target);
        E e1, e1_rev, e2, e2_rev;
        if (edge.cost > 0) {
            boost::tie(e1, added) = boost::add_edge(v1, v2, graph);
            boost::tie(e1_rev, added) =
                boost::add_edge(v2, v1, graph);
            E_to_id.insert(std::pair<E, int64_t>(e1, edge.id));
            E_to_id.insert(std::pair<E, int64_t>(e1_rev, edge.id));
            capacity[e1] = (int64_t) edge.cost;
            capacity[e1_rev] = 0;
            rev[e1] = e1_rev;
            rev[e1_rev] = e1;
        }
        if (edge.reverse_cost > 0) {
            boost::tie(e2, added) = boost::add_edge(v2, v1, graph);
            boost::tie(e2_rev, added) =
                boost::add_edge(v1, v2, graph);
            E_to_id.insert(std::pair<E, int64_t>(e2, edge.id));
            E_to_id.insert(std::pair<E, int64_t>(e2_rev, edge.id));
            capacity[e2] = (int64_t) edge.reverse_cost;
            capacity[e2_rev] = 0;
            rev[e2] = e2_rev;
            rev[e2_rev] = e2;
        }
    }
}

void PgrFlowGraph::insert_edges(
        const std::vector<pgr_edge_t> &edges) {
    bool added;
    for (const auto edge : edges) {
        V v1 = get_boost_vertex(edge.source);
        V v2 = get_boost_vertex(edge.target);
        E e, e_rev;
        boost::tie(e, added) = boost::add_edge(v1, v2, graph);
        boost::tie(e_rev, added) =
            boost::add_edge(v2, v1, graph);
        E_to_id.insert(std::pair<E, int64_t>(e, edge.id));
        E_to_id.insert(std::pair<E, int64_t>(e_rev, edge.id));
        capacity[e] = edge.cost > 0 ? (int64_t) edge.cost : 0;
        capacity[e_rev] = edge.reverse_cost > 0
            ? (int64_t) edge.reverse_cost : 0;
        rev[e] = e_rev;
        rev[e_rev] = e;
    }
}

void PgrFlowGraph::set_supersource(
        const std::set<int64_t> &source_vertices) {
    bool added;
    supersource = add_vertex(graph);
    for (int64_t source_id : source_vertices) {
        V source = get_boost_vertex(source_id);
        E e, e_rev;
        boost::tie(e, added) =
            boost::add_edge(supersource, source, graph);
        boost::tie(e_rev, added) =
            boost::add_edge(source, supersource, graph);

        capacity[e] = (std::numeric_limits<int32_t>::max)();
        /* From sources to supersource has 0 capacity*/
        capacity[e_rev] = 0;
        rev[e] = e_rev;
        rev[e_rev] = e;
    }
}

void PgrFlowGraph::set_supersink(
        const std::set<int64_t> &sink_vertices) {
    bool added;
    supersink = add_vertex(graph);
    for (int64_t sink_id : sink_vertices) {
        V sink = get_boost_vertex(sink_id);
        E e, e_rev;
        boost::tie(e, added) = boost::add_edge(sink, supersink, graph);
        boost::tie(e_rev, added) =
            boost::add_edge(supersink, sink, graph);
        /*
         * NOTE: int64_t crashes the server
         */
        /* From sinks to supersink has maximum capacity*/
        capacity[e] = (std::numeric_limits<int32_t>::max)();
        /* From supersink to sinks has 0 capacity*/
        capacity[e_rev] = 0;
        rev[e] = e_rev;
        rev[e_rev] = e;
    }
}

std::vector<pgr_flow_t>
PgrFlowGraph::get_flow_edges() const {
    std::vector<pgr_flow_t> flow_edges;
    E_it e, e_end;
    for (boost::tie(e, e_end) = boost::edges(graph); e != e_end;
            ++e) {
        // A supersource/supersink is used internally
        if (((capacity[*e] - residual_capacity[*e]) > 0) &&
                ((*e).m_source != supersource) &&
                ((*e).m_target != supersink)) {
            pgr_flow_t edge;
            edge.edge = get_edge_id(*e);
            edge.source = get_vertex_id((*e).m_source);
            edge.target = get_vertex_id((*e).m_target);
            edge.flow = capacity[*e] - residual_capacity[*e];
            edge.residual_capacity = residual_capacity[*e];
            flow_edges.push_back(edge);
        }
    }
    return flow_edges;
}

PgrFlowGraph:: PgrFlowGraph(
        pgr_basic_edge_t *data_edges,
        size_t total_tuples,
        const std::set<int64_t> &source_vertices,
        const std::set<int64_t> &sink_vertices,
        bool directed) {
    std::set<int64_t> vertices;
    for (int64_t source : source_vertices) {
        vertices.insert(source);
    }
    for (int64_t sink : sink_vertices) {
        vertices.insert(sink);
    }
    for (size_t i = 0; i < total_tuples; ++i) {
        vertices.insert(data_edges[i].source);
        vertices.insert(data_edges[i].target);
    }
    for (int64_t id : vertices) {
        V v = add_vertex(graph);
        id_to_V.insert(std::pair<int64_t, V>(id, v));
        V_to_id.insert(std::pair<V, int64_t>(v, id));
    }
    bool added;

    set_supersource(source_vertices);
    set_supersink(sink_vertices);

    capacity = get(boost::edge_capacity, graph);
    rev = get(boost::edge_reverse, graph);
    residual_capacity =
        get(boost::edge_residual_capacity, graph);

    for (size_t i = 0; i < total_tuples; ++i) {
        V v1 = get_boost_vertex(data_edges[i].source);
        V v2 = get_boost_vertex(data_edges[i].target);
        if (directed) {
            E e, e_rev;
            boost::tie(e, added) =
                boost::add_edge(v1, v2, graph);
            boost::tie(e_rev, added) =
                boost::add_edge(v2, v1, graph);
            E_to_id.insert(std::pair<E, int64_t>(e, data_edges[i].id));
            E_to_id.insert(std::pair<E, int64_t>(e_rev,
                        data_edges[i].id));
            capacity[e] = data_edges[i].going ? 1 : 0;
            capacity[e_rev] = data_edges[i].coming ? 1 : 0;
            rev[e] = e_rev;
            rev[e_rev] = e;
        } else {
            if (data_edges[i].going || data_edges[i].coming) {
                E e, e_rev;
                boost::tie(e, added) =
                    boost::add_edge(v1, v2, graph);
                boost::tie(e_rev, added) =
                    boost::add_edge(v2, v1, graph);
                E_to_id.insert(std::pair<E, int64_t>(e, data_edges[i].id));
                E_to_id.insert(std::pair<E, int64_t>(e_rev,
                            data_edges[i].id));
                capacity[e] = 1;
                capacity[e_rev] = 1;
                rev[e] = e_rev;
                rev[e_rev] = e;
            }
        }
    }
}

void
PgrFlowGraph::flow_dfs(V vertex,
        int64_t path_id,
        std::vector<std::vector<int64_t> > &paths) {
    Eout_it ei, e_end;
    if (boost::edge(vertex, supersink, graph).second) {
        int64_t v_id = get_vertex_id(vertex);
        paths[path_id].push_back(v_id);
    } else {
        for (boost::tie(ei, e_end) =
                boost::out_edges(vertex, graph);
                ei != e_end; ++ei) {
            if (residual_capacity[*ei] < capacity[*ei]) {
                // exclude this edge from subsequent visits
                capacity[*ei] = -1;
                int64_t v_id = get_vertex_id(vertex);
                paths[path_id].push_back(v_id);
                flow_dfs((*ei).m_target,
                        path_id,
                        paths);
                break;
            }
        }
    }
}

void
PgrFlowGraph::get_edge_disjoint_paths(std::vector<General_path_element_t> &path_elements,
        int64_t flow) {
    std::vector<std::vector<int64_t> > paths(flow, std::vector<int64_t>());
    int64_t path_id = 0;
    Eout_it ei, e_end, ei2, e2_end;
    for (boost::tie(ei, e_end) =
            boost::out_edges(supersource, graph);
            ei != e_end; ++ei) {
        if (capacity[*ei] - residual_capacity[*ei] > 0) {
            for (boost::tie(ei2, e2_end) =
                    boost::out_edges((*ei).m_target, graph);
                    ei2 != e2_end; ++ei2) {
                if (capacity[*ei2] - residual_capacity[*ei2]
                        > 0) {
                    paths[path_id].push_back(get_vertex_id((*ei2).m_source));
                    flow_dfs((*ei2).m_target, path_id, paths);
                    path_id++;
                }
            }
        }
    }
    for (int i = 0; i < flow; i++) {
        size_t size = paths[i].size();
        E e;
        bool exists;
        size_t j;
        for (j = 0; j < size - 1; j++) {
            General_path_element_t edge;
            edge.seq = static_cast<int>(j + 1);
            edge.start_id = paths[i][0];
            edge.end_id = paths[i][size - 1];
            edge.node = paths[i][j];
            boost::tie(e, exists) = boost::edge(get_boost_vertex(paths[i][j]),
                    get_boost_vertex(paths[i][j
                        + 1]),
                    graph);
            edge.edge = get_edge_id(e);
            path_elements.push_back(edge);
        }
        General_path_element_t edge;
        edge.seq = static_cast<int>(j + 1);
        edge.start_id = paths[i][0];
        edge.end_id = paths[i][size - 1];
        edge.node = paths[i][j];
        edge.edge = -1;
        path_elements.push_back(edge);
    }
}



}  // namespace graph
}  // namespace pgrouting

