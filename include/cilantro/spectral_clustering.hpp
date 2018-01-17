#pragma once

#include <memory>
#include <cilantro/kmeans.hpp>

//#include <cilantro/3rd_party/spectra/SymEigsSolver.h>
//#include <iostream>

namespace cilantro {

    enum struct GraphLaplacianType {UNNORMALIZED, NORMALIZED_SYMMETRIC, NORMALIZED_RANDOM_WALK};

    // EigenDim is the embedding dimension (and also the number of clusters); set to Eigen::Dynamic for runtime setting
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    class SpectralClustering {
    public:
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        // Number of clusters (embedding dimension) set at compile time
        template <ptrdiff_t Dim = EigenDim, class = typename std::enable_if<Dim != Eigen::Dynamic>::type>
        SpectralClustering(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                           const GraphLaplacianType &laplacian_type = GraphLaplacianType::NORMALIZED_RANDOM_WALK,
                           size_t kmeans_max_iter = 100,
                           ScalarT kmeans_conv_tol = std::numeric_limits<ScalarT>::epsilon(),
                           bool kmeans_use_kd_tree = false)
        {
            compute_dense_(affinities, EigenDim, false, laplacian_type, kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);
        }

        // Number of clusters (embedding dimension) set at runtime
        // Figures out number of clusters based of eigenvalue distribution if num_clusters == 0
        template <ptrdiff_t Dim = EigenDim, class = typename std::enable_if<Dim == Eigen::Dynamic>::type>
        SpectralClustering(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                           size_t max_num_clusters,
                           bool estimate_num_clusters = false,
                           const GraphLaplacianType &laplacian_type = GraphLaplacianType::NORMALIZED_RANDOM_WALK,
                           size_t kmeans_max_iter = 100,
                           ScalarT kmeans_conv_tol = std::numeric_limits<ScalarT>::epsilon(),
                           bool kmeans_use_kd_tree = false)
        {
            if (max_num_clusters > 0 && max_num_clusters <= affinities.rows()) {
                compute_dense_(affinities, max_num_clusters, estimate_num_clusters, laplacian_type, kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);
            } else {
                compute_dense_(affinities, affinities.rows(), estimate_num_clusters, laplacian_type, kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);
            }
        }

        ~SpectralClustering() {}

        inline const VectorSet<ScalarT,EigenDim>& getEmbeddedPoints() const { return embedded_points_; }

        inline const Vector<ScalarT,Eigen::Dynamic>& getUsedEigenValues() const { return eigenvalues_; }

        inline const std::vector<std::vector<size_t>>& getClusterPointIndices() const { return clusterer_->getClusterPointIndices(); }

        inline const std::vector<size_t>& getClusterIndexMap() const { return clusterer_->getClusterIndexMap(); }

        inline size_t getNumberOfClusters() const { return embedded_points_.rows(); }

        inline const KMeans<ScalarT,EigenDim>& getClusterer() const { return *clusterer_; }

    private:
        Vector<ScalarT,Eigen::Dynamic> eigenvalues_;
        VectorSet<ScalarT,EigenDim> embedded_points_;
        std::shared_ptr<KMeans<ScalarT,EigenDim>> clusterer_;

        void compute_dense_(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                            size_t max_num_clusters, bool estimate_num_clusters, const GraphLaplacianType &laplacian_type,
                            size_t kmeans_max_iter, ScalarT kmeans_conv_tol, bool kmeans_use_kd_tree)
        {
            size_t num_clusters = max_num_clusters;
            size_t num_eigenvalues = (estimate_num_clusters) ? std::min(max_num_clusters+1, (size_t)affinities.rows()) : max_num_clusters;

            switch (laplacian_type) {
                case GraphLaplacianType::UNNORMALIZED: {
                    Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> D = affinities.rowwise().sum().asDiagonal();
                    Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> L = D - affinities;

//                    Spectra::DenseSymMatProd<ScalarT> op(L);
//                    Spectra::SymEigsSolver<ScalarT, Spectra::SMALLEST_MAGN, Spectra::DenseSymMatProd<ScalarT>> eig(&op, num_eigenvalues, std::min(2*num_eigenvalues, (size_t)affinities.rows()));
//                    // Initialize and compute
//                    eig.init();
//                    eig.compute(1000, 1e-10, Spectra::SMALLEST_MAGN);
//
//                    eigenvalues_ = eig.eigenvalues();
//                    for (size_t i = 0; i < eigenvalues_.rows(); i++) {
//                        if (eigenvalues_[i] < 0.0) eigenvalues_[i] = 0.0;
//                    }
//                    if (estimate_num_clusters) {
//                        num_clusters = estimate_number_of_clusters_(eigenvalues_, max_num_clusters);
//                    }
//                    embedded_points_ = eig.eigenvectors().leftCols(num_clusters).transpose();

                    Eigen::SelfAdjointEigenSolver<Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> eig(L);
                    eigenvalues_ = eig.eigenvalues().head(num_eigenvalues);
                    for (size_t i = 0; i < eigenvalues_.rows(); i++) {
                        if (eigenvalues_[i] < 0.0) eigenvalues_[i] = 0.0;
                    }
                    if (estimate_num_clusters) {
                        num_clusters = estimate_number_of_clusters_(eigenvalues_, max_num_clusters);
                    }
                    embedded_points_ = eig.eigenvectors().leftCols(num_clusters).transpose();

                    clusterer_.reset(new KMeans<ScalarT,EigenDim>(embedded_points_));
                    clusterer_->cluster(num_clusters, kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);

                    break;
                }
                case GraphLaplacianType::NORMALIZED_SYMMETRIC: {
                    Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> Dtm12 = affinities.rowwise().sum().array().rsqrt().matrix().asDiagonal();
                    Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> L = Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>::Identity(affinities.rows(),affinities.cols()) - Dtm12*affinities*Dtm12;

                    Eigen::SelfAdjointEigenSolver<Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> eig(L);
                    eigenvalues_ = eig.eigenvalues().head(num_eigenvalues);
                    for (size_t i = 0; i < eigenvalues_.rows(); i++) {
                        if (eigenvalues_[i] < 0.0) eigenvalues_[i] = 0.0;
                    }
                    if (estimate_num_clusters) {
                        num_clusters = estimate_number_of_clusters_(eigenvalues_, max_num_clusters);
                    }
                    embedded_points_ = eig.eigenvectors().leftCols(num_clusters).transpose();

                    for (size_t i = 0; i < embedded_points_.cols(); i++) {
                        ScalarT scale = 1.0/embedded_points_.col(i).norm();
                        if (std::isfinite(scale)) embedded_points_.col(i) *= scale;
                    }

                    clusterer_.reset(new KMeans<ScalarT,EigenDim>(embedded_points_));
                    clusterer_->cluster(num_clusters, kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);

                    break;
                }
                case GraphLaplacianType::NORMALIZED_RANDOM_WALK: {
                    Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> D = affinities.rowwise().sum().asDiagonal();
                    Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> L = D - affinities;

                    Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> geig(L,D);
                    eigenvalues_ = geig.eigenvalues().head(num_eigenvalues);
                    for (size_t i = 0; i < eigenvalues_.rows(); i++) {
                        if (eigenvalues_[i] < 0.0) eigenvalues_[i] = 0.0;
                    }
                    if (estimate_num_clusters) {
                        num_clusters = estimate_number_of_clusters_(eigenvalues_, max_num_clusters);
                    }
                    embedded_points_ = geig.eigenvectors().leftCols(num_clusters).transpose();

                    clusterer_.reset(new KMeans<ScalarT,EigenDim>(embedded_points_));
                    clusterer_->cluster(num_clusters, kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);

                    break;
                }
            }
        }

        size_t estimate_number_of_clusters_(const Eigen::Ref<const Eigen::Matrix<ScalarT,EigenDim,1>> &eigenvalues,
                                            size_t max_num_clusters)
        {
            ScalarT min_val = std::numeric_limits<ScalarT>::infinity();
            ScalarT max_val = -std::numeric_limits<ScalarT>::infinity();
            ScalarT max_diff = eigenvalues[0];
            size_t max_ind = 0;
            for (size_t i = 0; i < eigenvalues.rows() - 1; i++) {
                ScalarT diff = eigenvalues[i+1] - eigenvalues[i];
                if (diff > max_diff) {
                    max_diff = diff;
                    max_ind = i;
                }
                if (eigenvalues[i] < min_val) min_val = eigenvalues[i];
                if (eigenvalues[i] > max_val) max_val = eigenvalues[i];
            }
            if (eigenvalues[eigenvalues.rows()-1] < min_val) min_val = eigenvalues[eigenvalues.rows()-1];
            if (eigenvalues[eigenvalues.rows()-1] > max_val) max_val = eigenvalues[eigenvalues.rows()-1];

            if (max_val - min_val < std::numeric_limits<ScalarT>::epsilon()) return max_num_clusters;
            return max_ind + 1;
        }

    };
}