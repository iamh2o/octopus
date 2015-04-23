from math import log, exp, copysign
from scipy.special import digamma

def log_sum_exp(xs):
    m = max(xs)
    return m + log(sum([exp(x - m) for x in xs]))

def two_hap_posteriors(alpha1, alpha2, log_like_hom1, log_like_het, log_like_hom2):
    s = 2 * digamma(alpha1 +  alpha2)
    log_hom1 = 2 * digamma(alpha1) - s + log_like_hom1
    log_het  = log(2) + digamma(alpha1) + digamma(alpha2) - s + log_like_het
    log_hom2 = 2 * digamma(alpha2) - s + log_like_hom2
    norm = log_sum_exp([log_hom1, log_het, log_hom2])
    post_hom1 = exp(log_hom1 - norm)
    post_het  = exp(log_het - norm)
    post_hom2 = exp(log_hom2 - norm)
    return (post_hom1, post_het, post_hom2)

def three_hap_posteriors(alpha1, alpha2, alpha3, ll_11, ll_12, ll_13, ll_22, ll_23, ll_33):
    s = 2 * digamma(alpha1 + alpha2 + alpha3)
    log_11 = 2 * digamma(alpha1) - s + ll_11
    log_12  = log(2) + digamma(alpha1) + digamma(alpha2) - s + ll_12
    log_13  = log(2) + digamma(alpha1) + digamma(alpha3) - s + ll_13
    log_22 = 2 * digamma(alpha2) - s + ll_22
    log_23 = log(2) + digamma(alpha2) + digamma(alpha3) - s + ll_23
    log_33 = 2 * digamma(alpha3) - s + ll_33
    norm = log_sum_exp([log_11, log_12, log_13, log_22, log_23, log_33])
    post_11 = exp(log_11 - norm)
    post_12 = exp(log_12 - norm)
    post_13 = exp(log_13 - norm)
    post_22 = exp(log_22 - norm)
    post_23 = exp(log_23 - norm)
    post_33 = exp(log_33 - norm)
    return (post_11, post_12, post_13, post_22, post_23, post_33)

sign = lambda x: math.copysign(1, x)

def idigamma(x):
    l = 1.0
    y = exp(x)
    while l > 10e-8:
        y += sign(x - digamma(y))
        l /= 2
    return y

def dirichlet_pdf(x, alpha):
    return (gamma(sum(alpha)) /
            reduce(operator.mul, [gamma(a) for a in alpha]) *
            reduce(operator.mul, [x[i]**(alpha[i]-1.0) for i in range(len(alpha))]))
