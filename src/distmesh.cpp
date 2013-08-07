// libdistmesh
//
// Copyright (C) 2013  Patrik Gebhardt
// Contact: patrik.gebhardt@rub.de

#include "distmesh/distmesh.h"

// apply the distmesh algorithm
std::tuple<std::shared_ptr<distmesh::dtype::array<distmesh::dtype::real>>,
    std::shared_ptr<distmesh::dtype::array<distmesh::dtype::index>>>
    distmesh::distmesh(
    std::function<dtype::real(dtype::array<dtype::real>)> distance_function,
    std::function<dtype::real(dtype::array<dtype::real>)> edge_length_function,
    dtype::real initial_edge_length, dtype::array<dtype::real> bounding_box) {
    // create initial distribution in bounding_box
    auto points = meshgen::create_point_list(distance_function,
        edge_length_function, initial_edge_length, bounding_box);

    // create initial triangulation
    auto triangulation = triangulation::delaunay(points);

    // create array of points of last iteration, but initialize with inf for
    // first iteration
    auto old_points = std::make_shared<dtype::array<dtype::real>>(
        points->rows(), points->cols());
    old_points->fill(INFINITY);

    // main distmesh loop
    std::shared_ptr<dtype::array<dtype::index>> bar_indices = nullptr;
    for (int i = 0; i < 100; ++i) {
//    while (1) {
        // retriangulate if point movement is above tolerance
        auto retriangulation_criterium =
            (((*points) - (*old_points)).square().rowwise().sum() /
            initial_edge_length).maxCoeff();
        if (retriangulation_criterium > settings::retriangulation_tolerance) {
            *old_points = *points;

            // reject triangles with circumcenter outside of the region
            triangulation = triangulation::delaunay(points);
            dtype::array<dtype::index> keep_triangulation(triangulation->rows(), triangulation->cols());
            dtype::index triangle_count = 0;
            for (dtype::index triangle = 0; triangle < triangulation->rows(); ++triangle) {
                // calculate circumcenter
                dtype::array<dtype::real> circumcenter(1, points->cols());
                circumcenter.fill(0.0);
                for (dtype::index point = 0; point < triangulation->cols(); ++point) {
                    circumcenter += points->row((*triangulation)(triangle, point)) / triangulation->cols();
                }
                if (distance_function(circumcenter) < -settings::general_precision * initial_edge_length) {
                    keep_triangulation.row(triangle_count) = triangulation->row(triangle);
                    triangle_count++;
                }
            }
            *triangulation = keep_triangulation;
            triangulation->conservativeResize(triangle_count, triangulation->cols());

            // find unique bar indices
            bar_indices = meshgen::find_unique_bars(points, triangulation);
        }

        // calculate bar vector
        dtype::array<dtype::real> bar_vector(bar_indices->rows(), points->cols());
        bar_vector.fill(0.0);
        for (dtype::index bar = 0; bar < bar_indices->rows(); ++bar) {
            bar_vector.row(bar) += points->row((*bar_indices)(bar, 0)) -
                points->row((*bar_indices)(bar, 1));
        }

        // calculate length of each bar
        dtype::array<dtype::real> bar_length(bar_indices->rows(), 1);
        bar_length = bar_vector.square().rowwise().sum().sqrt();

        // evaluate edge_length_function
        dtype::array<dtype::real> hbars(bar_indices->rows(), 1);
        for (dtype::index bar = 0; bar < bar_indices->rows(); ++bar) {
            hbars(bar, 0) = edge_length_function(0.5 * (points->row((*bar_indices)(bar, 0)) +
                points->row((*bar_indices)(bar, 1))));
        }

        // calculate desired bar length
        dtype::array<dtype::real> desired_bar_length(bar_length.rows(), bar_length.cols());
        desired_bar_length = hbars * (1.0 + 0.4 / std::pow(2.0, points->cols() - 1)) *
            std::pow((bar_length.pow(points->cols()).sum() / hbars.pow(points->cols()).sum()), 1.0 / points->cols());

        // calculate force vector for each bar
        dtype::array<dtype::real> force(bar_indices->rows(), 1);
        dtype::array<dtype::real> force_vector(bar_indices->rows(), points->cols());
        force = ((desired_bar_length - bar_length) / bar_length).max(0.0);
        for (dtype::index dim = 0; dim < points->cols(); ++dim) {
            force_vector.col(dim) = force * bar_vector.col(dim);
        }

        // move points
        for (dtype::index bar = 0; bar < bar_indices->rows(); ++bar) {
            points->row((*bar_indices)(bar, 0)) += settings::deltaT *
                force_vector.row(bar);
            points->row((*bar_indices)(bar, 1)) -= settings::deltaT *
                force_vector.row(bar);
        }
    }

    return std::make_tuple(points, triangulation);
}