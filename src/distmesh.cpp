// --------------------------------------------------------------------
// This file is part of libDistMesh.
//
// libDistMesh is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// libDistMesh is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with libDistMesh.  If not, see <http://www.gnu.org/licenses/>.
//
// Copyright (C) 2013 Patrik Gebhardt
// Contact: patrik.gebhardt@rub.de
// --------------------------------------------------------------------

#include "distmesh/distmesh.h"
#include <vector>
#include <set>
#include <algorithm>

// easy creation of n-dimensional bounding_box
distmesh::dtype::array<distmesh::dtype::real> distmesh::bounding_box(
    dtype::index dimension) {
    dtype::array<dtype::real> box(dimension, 2);
    box.col(0).fill(-1.0);
    box.col(1).fill(1.0);
    return box;
}

// apply the distmesh algorithm
std::tuple<distmesh::dtype::array<distmesh::dtype::real>,
    distmesh::dtype::array<distmesh::dtype::index>> distmesh::distmesh(
    Functional distance_function, dtype::real edge_length_base,
    Functional edge_length_function, dtype::array<dtype::real> bounding_box,
    dtype::array<dtype::real> fixed_points) {
    // create initial distribution in bounding_box
    dtype::array<dtype::real> points = utils::create_point_list(distance_function,
        edge_length_base, edge_length_function, bounding_box, fixed_points);

    // create initial triangulation
    dtype::array<dtype::index> triangulation = triangulation::delaunay(points);

    // create points buffer for retriangulation and stop criterion
    dtype::array<dtype::real> buffer_retriangulation_criterion(
        points.rows(), points.cols());
    dtype::array<dtype::real> buffer_stop_criterion;
    buffer_retriangulation_criterion.fill(INFINITY);

    // main distmesh loop
    dtype::array<dtype::index> bar_indices;
    for (dtype::index step = 0; step < settings::max_steps; ++step) {
        // retriangulate if point movement is above tolerance
        dtype::real retriangulation_criterion =
            ((points - buffer_retriangulation_criterion).square().rowwise().sum().sqrt() /
            edge_length_base).maxCoeff();
        if (retriangulation_criterion > settings::retriangulation_tolerance) {
            // update triangulation
            triangulation = triangulation::delaunay(points);

            // calculate circumcenter
            dtype::array<dtype::real> circumcenter = dtype::array<dtype::real>::Zero(
                triangulation.rows(), points.cols());
            for (dtype::index point = 0; point < triangulation.cols(); ++point) {
                circumcenter += utils::select_indexed_array_elements<dtype::real>(
                    points, triangulation.col(point)) / triangulation.cols();
            }

            // reject triangles with circumcenter outside of the region
            dtype::array<bool> circumcenter_criterion = distance_function(circumcenter) <
                -settings::general_precision * edge_length_base;
            triangulation = utils::select_masked_array_elements<dtype::index>(triangulation,
                circumcenter_criterion);

            // find unique bar indices
            bar_indices = utils::find_unique_bars(triangulation);

            buffer_retriangulation_criterion = points;
        }

        // calculate bar vectors and their length
        dtype::array<dtype::real> bar_vector =
            utils::select_indexed_array_elements<dtype::real>(points, bar_indices.col(0)) -
            utils::select_indexed_array_elements<dtype::real>(points, bar_indices.col(1));
        dtype::array<dtype::real> bar_length = bar_vector.square().rowwise().sum().sqrt();

        // evaluate edge_length_function at midpoints of bars
        dtype::array<dtype::real> bar_midpoints = 0.5 *
            (utils::select_indexed_array_elements<dtype::real>(points, bar_indices.col(0)) +
            utils::select_indexed_array_elements<dtype::real>(points, bar_indices.col(1)));
        dtype::array<dtype::real> hbars = edge_length_function(bar_midpoints);

        // calculate desired bar length
        dtype::array<dtype::real> desired_bar_length = hbars *
            (1.0 + 0.4 / std::pow(2.0, points.cols() - 1)) *
            std::pow((bar_length.pow(points.cols()).sum() /
            hbars.pow(points.cols()).sum()), 1.0 / points.cols());

        // calculate force vector for each bar
        dtype::array<dtype::real> force_vector = bar_vector.colwise() *
            ((desired_bar_length - bar_length) / bar_length).max(0.0).col(0);

        // move points
        buffer_stop_criterion = points;
        for (dtype::index bar = 0; bar < bar_indices.rows(); ++bar) {
            if (bar_indices(bar, 0) >= fixed_points.rows()) {
                points.row(bar_indices(bar, 0)) += settings::deltaT * force_vector.row(bar);
            }
            if (bar_indices(bar, 1) >= fixed_points.rows()) {
                points.row(bar_indices(bar, 1)) -= settings::deltaT * force_vector.row(bar);
            }
        }

        // project points outside of domain to boundary
        utils::project_points_to_function(distance_function,
            edge_length_base, points);

        // stop criterion
        dtype::real stop_criterion = ((points - buffer_stop_criterion)
            .square().rowwise().sum().sqrt() / edge_length_base)
            .maxCoeff();
        if (stop_criterion < settings::point_movement_tolerance) {
            break;
        }
    }

    return std::make_tuple(points, triangulation);
}

// determine boundary edges of given triangulation
distmesh::dtype::array<distmesh::dtype::index> distmesh::boundedges(
    const Eigen::Ref<dtype::array<dtype::index>>& triangulation) {
    std::set<std::vector<dtype::index>> edge_set;
    std::vector<std::vector<dtype::index>> boundary_edges;
    std::vector<dtype::index> edge(triangulation.cols() - 1);

    // find all possible combinations of edges
    dtype::array<dtype::index> combinations = utils::n_over_k(triangulation.cols(),
        triangulation.cols() - 1);

    // find edges, which only appear once in triangulation
    for (dtype::index combination = 0; combination < combinations.rows(); ++combination)
    for (dtype::index triangle = 0; triangle < triangulation.rows(); ++triangle) {
        // get current edge
        for (dtype::index vertex = 0; vertex < edge.size(); ++vertex) {
            edge[vertex] = triangulation(triangle, combinations(combination, vertex));
        }

        // insert edge in set to get info about multiple appearance
        if (!std::get<1>(edge_set.insert(edge))) {
            // find edge in vector and delete it
            auto it = std::find(boundary_edges.begin(), boundary_edges.end(), edge);
            if (it != boundary_edges.end()) {
                boundary_edges.erase(it);
            }
        } else {
            boundary_edges.push_back(edge);
        }
    }

    // convert stl vector to eigen array
    dtype::array<dtype::index> boundary_array(boundary_edges.size(), edge.size());
    for (dtype::index edge = 0; edge < boundary_array.rows(); ++edge)
    for (dtype::index vertex = 0; vertex < boundary_array.cols(); ++vertex) {
        boundary_array(edge, vertex) = boundary_edges[edge][vertex];
    }

    return boundary_array;
}
