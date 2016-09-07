#include "metric.hpp"

#include "access.hpp"
#include "loop.hpp"
#include "size.hpp"

namespace Omega_h {

template <Int sdim, Int edim>
static Reals average_metric_tmpl(Mesh* mesh, LOs a2e, Reals v2m) {
  auto na = a2e.size();
  Write<Real> out(na * symm_dofs(sdim));
  auto ev2v = mesh->ask_verts_of(edim);
  auto f = LAMBDA(LO a) {
    auto e = a2e[a];
    auto v = gather_verts<edim + 1>(ev2v, e);
    auto ms = gather_symms<edim + 1, sdim>(v2m, v);
    auto m = average_metrics(ms);
    set_symm(out, a, m);
  };
  parallel_for(na, f);
  return out;
}

Reals average_metric(Mesh* mesh, Int ent_dim, LOs entities, Reals v2m) {
  if (mesh->dim() == 3) {
    if (ent_dim == 3) {
      return average_metric_tmpl<3, 3>(mesh, entities, v2m);
    } else if (ent_dim == 2) {
      return average_metric_tmpl<3, 2>(mesh, entities, v2m);
    } else {
      CHECK(ent_dim == 1);
      return average_metric_tmpl<3, 1>(mesh, entities, v2m);
    }
  } else {
    CHECK(mesh->dim() == 2);
    if (ent_dim == 2) {
      return average_metric_tmpl<2, 2>(mesh, entities, v2m);
    } else {
      CHECK(ent_dim == 1);
      return average_metric_tmpl<2, 1>(mesh, entities, v2m);
    }
  }
}

template <Int dim>
Reals interpolate_metrics(Reals a, Reals b, Real t) {
  CHECK(a.size() == b.size());
  CHECK(a.size() % symm_dofs(dim) == 0);
  auto n = a.size() / symm_dofs(dim);
  auto out = Write<Real>(n * symm_dofs(dim));
  auto f = LAMBDA(LO i) {
    auto am = get_symm<dim>(a, i);
    auto bm = get_symm<dim>(b, i);
    auto cm = interpolate_metrics(am, bm, t);
    set_symm(out, i, cm);
  };
  parallel_for(n, f);
  return out;
}

Reals interpolate_metrics(Int dim, Reals a, Reals b, Real t) {
  if (dim == 3) return interpolate_metrics<3>(a, b, t);
  CHECK(dim == 2);
  return interpolate_metrics<2>(a, b, t);
}

template <Int dim>
Reals linearize_metrics_dim(Reals metrics) {
  auto n = metrics.size() / symm_dofs(dim);
  auto out = Write<Real>(n * dim * dim);
  auto f = LAMBDA(LO i) {
    set_matrix(out, i, linearize_metric(get_symm<dim>(metrics, i)));
  };
  parallel_for(n, f);
  return out;
}

template <Int dim>
Reals delinearize_metrics_dim(Reals lms) {
  auto n = lms.size() / square(dim);
  auto out = Write<Real>(n * symm_dofs(dim));
  auto f = LAMBDA(LO i) {
    set_symm(out, i, delinearize_metric(get_matrix<dim>(lms, i)));
  };
  parallel_for(n, f);
  return out;
}

Reals linearize_metrics(Int dim, Reals metrics) {
  CHECK(metrics.size() % symm_dofs(dim) == 0);
  if (dim == 3) return linearize_metrics_dim<3>(metrics);
  if (dim == 2) return linearize_metrics_dim<2>(metrics);
  NORETURN(Reals());
}

Reals delinearize_metrics(Int dim, Reals linear_metrics) {
  CHECK(linear_metrics.size() % square(dim) == 0);
  if (dim == 3) return delinearize_metrics_dim<3>(linear_metrics);
  if (dim == 2) return delinearize_metrics_dim<2>(linear_metrics);
  NORETURN(Reals());
}

template <Int dim>
static Few<Reals, dim> axes_from_metrics_dim(Reals metrics) {
  CHECK(metrics.size() % symm_dofs(dim) == 0);
  auto n = metrics.size() / symm_dofs(dim);
  Few<Write<Real>, dim> w;
  for (Int i = 0; i < dim; ++i) w[i] = Write<Real>(n * dim);
  auto f = LAMBDA(LO i) {
    auto md = decompose_metric(get_symm<dim>(metrics, i));
    for (Int j = 0; j < dim; ++j) set_vector(w[j], i, md.q[j] * md.l[j]);
  };
  parallel_for(n, f);
  Few<Reals, dim> r;
  for (Int i = 0; i < dim; ++i) r[i] = Reals(w[i]);
  return r;
}

template <Int dim>
static void axes_from_metric_field_dim(Mesh* mesh, std::string const& metric_name,
    std::string const& output_prefix) {
  auto metrics = mesh->get_array<Real>(VERT, metric_name);
  auto axes = axes_from_metrics_dim<dim>(metrics);
  for (Int i = 0; i < dim; ++i) {
    mesh->add_tag(VERT, output_prefix + '_' + std::to_string(i), dim,
        OMEGA_H_DONT_TRANSFER, OMEGA_H_DO_OUTPUT, axes[i]);
  }
}

void axes_from_metric_field(Mesh* mesh, std::string const& metric_name,
    std::string const& axis_prefix) {
  if (mesh->dim() == 3) {
    axes_from_metric_field_dim<3>(mesh, metric_name, axis_prefix);
    return;
  }
  if (mesh->dim() == 2) {
    axes_from_metric_field_dim<2>(mesh, metric_name, axis_prefix);
    return;
  }
  NORETURN();
}

/* A Hessian-based anisotropic size field, from
 * Alauzet's tech report:
 *
 * F. Alauzet, P.J. Frey, Estimateur d'erreur geometrique
 * et metriques anisotropes pour l'adaptation de maillage.
 * Partie I: aspects theoriques,
 * RR-4759, INRIA Rocquencourt, 2003.
 */

template <Int dim>
static INLINE Matrix<dim, dim> metric_from_hessian(
    Matrix<dim, dim> hessian, Real eps, Real hmin, Real hmax) {
  auto ed = decompose_eigen(hessian);
  auto r = ed.q;
  auto l = ed.l;
  constexpr auto c_num = square(dim);
  constexpr auto c_denom = 2 * square(dim + 1);
  decltype(l) tilde_l;
  for (Int i = 0; i < dim; ++i) {
    auto val = (c_num * fabs(l[i])) / (c_denom * eps);
    tilde_l[i] = min2(max2(val, 1. / square(hmax)), 1. / square(hmin));
  }
  return compose_eigen(r, tilde_l);
}

template <Int dim>
static Reals metric_from_hessians_dim(
    Reals hessians, Real eps, Real hmin, Real hmax) {
  auto ncomps = symm_dofs(dim);
  CHECK(hessians.size() % ncomps == 0);
  auto n = hessians.size() / ncomps;
  auto out = Write<Real>(n * ncomps);
  auto f = LAMBDA(LO i) {
    auto hess = get_symm<dim>(hessians, i);
    auto m = metric_from_hessian(hess, eps, hmin, hmax);
    set_symm(out, i, m);
  };
  parallel_for(n, f);
  return out;
}

Reals metric_from_hessians(
    Int dim, Reals hessians, Real eps, Real hmin, Real hmax) {
  CHECK(hmin > 0);
  CHECK(hmax > 0);
  CHECK(hmin <= hmax);
  CHECK(eps > 0);
  if (dim == 3) return metric_from_hessians_dim<3>(hessians, eps, hmin, hmax);
  if (dim == 2) return metric_from_hessians_dim<2>(hessians, eps, hmin, hmax);
  NORETURN(Reals());
}

Reals metric_for_nelems_from_hessians(Mesh* mesh, Real target_nelems,
    Real tolerance, Reals hessians, Real hmin, Real hmax) {
  CHECK(tolerance > 0);
  CHECK(target_nelems > 0);
  auto dim = mesh->dim();
  Real scalar;
  Reals metric;
  Real eps = 1.0;
  Int niters = 0;
  do {
    metric = metric_from_hessians(dim, hessians, eps, hmin, hmax);
    scalar = metric_scalar_for_nelems(mesh, metric, target_nelems);
    eps /= scalar;
    ++niters;
  } while (fabs(scalar - 1.0) > tolerance);
  if (mesh->comm()->rank() == 0) {
    std::cout << "after " << niters << " iterations,"
              << " metric targets " << target_nelems << "*" << scalar
              << " elements\n";
  }
  return metric;
}

}  // end namespace Omega_h
