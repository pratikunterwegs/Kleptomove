// Feed forward neural networks
//
// Hanno 2016, 2017


#ifndef ANN_HPP_INCLUDED
#define ANN_HPP_INCLUDED

#include <memory>
#include <type_traits>
#include <algorithm>
#include <array>
#include <tuple>
#include <cassert>


#if defined(__INTEL_COMPILER)
    #define ann_assume_aligned(x, a) __assume_aligned(x, a)
#elif defined(_MSC_VER)
    #define ann_assume_aligned(x, a) __assume((((char*)x) - ((char*) 0)) % (a) == 0)
#else
    #define ann_assume_aligned(x, a)
#endif


namespace ann {


template <size_t size, size_t scratch = 0>
struct extra_state 
{
  static const size_t value = size;
  static const size_t scratch = scratch;
};



namespace activation {

  //! \brief zero activation function [] <- u
  struct zero : extra_state<0>
  {
    template <typename T> static constexpr T min = T(0);
    template <typename T> static constexpr T max = T(0);

    template <typename T>
    static T apply(T, const T* __restrict)
    {
      return T(0);
    }
  };


  //! \brief pass-through (no-operation) activation function [] <- u
  struct identity : extra_state<0>
  {
    template <typename T> static constexpr T min = -std::numeric_limits<T>::max();
    template <typename T> static constexpr T max = +std::numeric_limits<T>::max();

    template <typename T>
    static T apply(T u, const T* __restrict)
    {
      return u;
    }
  };


  //! \brief Hard limit (step) activation
  struct sgn
  {
    //! \brief bipolar hard limit activation [-1,1] <- u
    struct bipolar : extra_state<0>
    {
      template <typename T> static constexpr T min = T(-1);
      template <typename T> static constexpr T max = T(+1);

      template <typename T>
      static T apply(T u, const T* __restrict)
      {
        return u > T(0) ? T(-1) : T(1);
      }
    };


    //! \brief unipolar hard limit activation [0,1] <- u
    struct unipolar : extra_state<0>
    {
      template <typename T> static constexpr T min = T(0);
      template <typename T> static constexpr T max = T(1);

      template <typename T>
      static T apply(T u, const T* __restrict)
      {
        return u > T(0) ? T(0) : T(1);
      }
    };
  };

  
  //! \brief Rectified linear activation [0,u] <- u
  struct rtlu : extra_state<0>
  {
    template <typename T> static constexpr T min = T(0);
    template <typename T> static constexpr T max = std::numeric_limits<T>::max();

    template <typename T>
    static T apply(T u, const T* __restrict)
    {
      return u = std::max(T(0), u);
    }
  };


  //! \brief hyperbolic activation
  struct tanh
  {
    //! brief bipolar tangent activation [-1,1] <- u
    struct bipolar : extra_state<0>
    {
      template <typename T> static constexpr T min = T(-1);
      template <typename T> static constexpr T max = T(+1);

      template <typename T>
      static T apply(T u, const T* __restrict)
      {
        return std::tanh(u);
      }
    };


    //! brief unipolar tangent activation [0,1] <- u
    struct unipolar : extra_state<0>
    {
      template <typename T> static constexpr T min = T(0);
      template <typename T> static constexpr T max = T(1);

      template <typename T>
      static T apply(T u, const T* __restrict)
      {
        return T(0.5) * (std::tanh(u) + T(1));
      }
    };

  };


  //! \brief sigmoid activation
  struct sig
  {
    //! \brief sigmoid activation [-1, 1] <- u
    //! \tparam n Nominator of the slope parameter
    //! \tparam d Denominator of the slope parameter
    template <int n = 1, int d = 1>
    struct bipolar : extra_state<0>
    {
      template <typename T> static constexpr T min = T(-1);
      template <typename T> static constexpr T max = T(+1);

      template <typename T>
      static T apply(T u, const T* __restrict)
      {
        const T a = static_cast<T>(-n) / d;
        const auto Exp = std::exp(a * u);
        return (T(1) - Exp) / (T(1) + Exp);
      }
    };


    //! \brief sigmoid activation [0, 1] < u
    //! \tparam n Nominator of the slope parameter
    //! \tparam d Denominator of the slope parameter
    template <int n = 1, int d = 1>
    struct unipolar : extra_state<0>
    {
      template <typename T> static constexpr T min = T(0);
      template <typename T> static constexpr T max = T(1);

      template <typename T>
      static T apply(T u, const T* __restrict)
      {
        const T a = static_cast<T>(-n) / d;
        return T(1) / (T(1) + std::exp(a * u));
      }
    };
  };


  //! \brief sigmoid activation with varying shape parameter
  struct varsig
  {
    //! \brief sigmoid activation with varying shape parameter [-1,1] <- u
    struct bipolar : extra_state<1>
    {
      template <typename T> static constexpr T min = T(-1);
      template <typename T> static constexpr T max = T(+1);

      template <typename T>
      static T apply(T u, const T* __restrict ps)
      {
        const auto Exp = std::exp(-ps[0] * u);
        return (T(1) - Exp) / (T(1) + Exp);
      }
    };


    //! \brief sigmoid activation with varying shape parameter [0,1] <- u
    struct unipolar : extra_state<1>
    {
      template <typename T> static constexpr T min = T(0);
      template <typename T> static constexpr T max = T(1);

      template <typename T>
      static T apply(T u, const T* __restrict ps)
      {
        return T(1) / (T(1) + std::exp(-ps[0] * u));
      }
    };
  };
}


namespace feedback {

  //! \brief no feedback
  struct none : extra_state<0>
  {
    template <typename T>
    static T apply(T u, const T* __restrict, const T* __restrict)
    {
      return u;
    }
  };


  //! \brief direct feedback of \p last_u  * \p f
  struct direct : extra_state<1,1>
  {
    template <typename T>
    static T apply(T u, const T* __restrict ps, T* __restrict scratch)
    {
      return scratch[0] = u + ps[0] * scratch[0];
    }
  };
}


template <size_t Input, 
          typename Activation, 
          typename Feedback = feedback::none, 

          bool Biased = true //with or without biased nodes

>
struct Neuron
{
  using neuron_t = Neuron;
  using activation_t = Activation;
  using feedback_t = Feedback;


  static constexpr bool biased = Biased;                                               // flag
  static constexpr size_t input_size = Input;                                          // inputs
  static constexpr size_t input_weights = input_size + (biased ? 1 : 0);               // ordinary weights
  static constexpr size_t activation_state = Activation::value;                        // activation state size
  static constexpr size_t feedback_state = Feedback::value;                            // feedback state size
  static constexpr size_t feedback_scratch = Feedback::scratch;                        // feedback scratch size
  static constexpr size_t total_weights = input_weights + 
                                          activation_state + 
                                          feedback_state;                              // total weights
  static constexpr size_t state_size = total_weights + feedback_scratch;               // total state size
  static constexpr size_t activation_begin = input_weights;                            // offset activation state in state
  static constexpr size_t feedback_begin = activation_begin + activation_state;        // offset feedback state in state
  static constexpr size_t feedback_scratch_begin = feedback_begin + feedback_state;    // offset feedback scratch in state


  template <typename T>
  static auto feed(const std::array<T, input_size>& in, T* __restrict const state)
    -> std::enable_if_t<biased, T>
  {
    auto u = state[0];    // bias
    for (size_t i = 1; i <= input_size; ++i) {
      u += state[i] * in[i - 1];
    }
    return activation_t::apply(feedback_t::apply(u, state + feedback_begin, state + feedback_scratch_begin), state + activation_begin);
  }


  template <typename T>
  static auto feed(const std::array<T, input_size>& in, T* __restrict const state)
    -> std::enable_if_t<!biased, T>
  {
    auto u = T(0);
    for (size_t i = 0; i < input_size; ++i) {
      u += state[i] * in[i];
    }
    return activation_t::apply(feedback_t::apply(u, state + feedback_begin, state + feedback_scratch_begin), state + activation_begin);
  }
};


template <size_t Input, typename Activation, typename Feedback = feedback::none>
using UnbiasedNeuron = Neuron<Input, Activation, Feedback, false>;


template <typename Neuron, size_t N>
struct Layer
{
  using layer_t = Layer;
  using neuron_t = Neuron;

  static constexpr size_t size = N;                                    // number of neurons
  static constexpr size_t input_size = neuron_t::input_size;           // inputs
  static constexpr size_t state_size = size * neuron_t::state_size;    // total state
  static constexpr size_t output_size = size;                          // outputs
  template <typename T> static constexpr T min_output = Neuron::template min<T>;
  template <typename T> static constexpr T max_output = Neuron::template max<T>;

  template <size_t Ofs, typename T>
  static std::array<T, output_size> feed(const std::array<T, input_size>& in, T* __restrict state)
  {
    alignas(16) std::array<T, output_size> out;
    T* pout = out.data();
    ann_assume_aligned(pout, 16);
    ann_assume_aligned(state, 16);
    state += Ofs;
    for (size_t i = 0; i < output_size; ++i, state += neuron_t::state_size) {
      pout[i] = neuron_t::feed(in, state);
    }
    return out;
  }

};


namespace detail {

  template <size_t I, typename C>
  struct accum_state_size 
    : extra_state<std::tuple_element_t<I, C>::state_size + accum_state_size<I - 1, C>::value>
  {
  };


  template <typename C>
  struct accum_state_size<0, C> 
    : std::integral_constant<size_t, std::tuple_element_t<0, C>::state_size>
  {
  };


  template <typename... L>
  struct network_state_size 
    : accum_state_size<sizeof...(L) - 1, std::tuple<L...>>
  {
  };


  template <size_t I, typename C>
  struct accum_state_ofs 
    : extra_state<accum_state_size<I, C>::value - std::tuple_element_t<I, C>::state_size>
  {
  };


  template <typename L0, typename L1, typename... L>
  struct match_interface_impl 
    : std::integral_constant<bool, L0::output_size == L1::input_size && match_interface_impl<L1, L...>::value>
  {
  };


  template <typename L0, typename L1>
  struct match_interface_impl<L0, L1> 
    : std::integral_constant<bool, L0::output_size == L1::input_size>
  {
  };


  template <size_t I, typename... L>
  struct match_interface : match_interface_impl<L...>
  {
  };


  template <typename... L>
  struct match_interface<1, L...> : std::integral_constant<bool, true>
  {
  };
}


template <typename T, typename... L>
class alignas(16) Network
{
public:
  static_assert(std::is_arithmetic<T>::value, "Network::value_type shall be arithmetic");
  static_assert(detail::match_interface<sizeof...(L), L...>::value, "Network: layer interfaces don't match");
  static constexpr size_t size = sizeof...(L);

  using value_type = T;
  using network_t = Network;
  using layer_t = std::tuple<L...>;
  using input_layer_t = std::tuple_element_t<0, layer_t>;
  using output_layer_t = std::tuple_element_t<size - 1, layer_t>;

  static constexpr size_t output_layer = size - 1;
  static constexpr size_t input_size = input_layer_t::input_size;
  static constexpr size_t output_size = output_layer_t::output_size;
  static constexpr size_t state_size = detail::network_state_size<L...>::value;

  using input_t = std::array<value_type, input_size>;
  using output_t = std::array<value_type, output_size>;


  Network()
  {
  }


  explicit Network(value_type val)
  : Network()
  {
    for (auto& s : state_) { s = val; };
  }


  // State access functions
  const value_type* cbegin() const { return state_.data(); }
  const value_type* cend() const { return state_.data() + state_size; }
  const value_type* begin() const { return state_.data(); }
  const value_type* end() const { return state_.data() + state_size; }
  value_type* begin() { return state_.data(); }
  value_type* end() { return state_.data() + state_size; }


  // Layer access functions
  template <size_t I>
  auto get_layer() const
  {
    static_assert(I < std::tuple_size<layer_t>::value, "NetworkImpl::get index out of range");
    using layerI = std::tuple_element_t<I, layer_t>;
    return std::make_pair(layerI(), state_.data() + detail::accum_state_ofs<I, layer_t>::value);
  }


  // Neuron access functions
  template <size_t I, size_t J>
  auto get_neuron() const
  {
    static_assert(I < std::tuple_size<layer_t>::value, "NetworkImpl::get index out of range");
    using layerI = std::tuple_element_t<I, layer_t>;
    static_assert(J < layerI::size, "NetworkImpl::get index out of range");
    return std::make_pair(typename layerI::neuron_t(), state_.data() + detail::accum_state_ofs<I, layer_t>::value + J * layerI::neuron_t::state_size);
  }


  // Neuron access functions
  template <size_t I, size_t J>
  auto get_neuron()
  {
    static_assert(I < std::tuple_size<layer_t>::value, "NetworkImpl::get index out of range");
    using layerI = std::tuple_element_t<I, layer_t>;
    static_assert(J < layerI::size, "NetworkImpl::get index out of range");
    return std::make_pair(typename layerI::neuron_t(), state_.data() + detail::accum_state_ofs<I, layer_t>::value + J * layerI::neuron_t::state_size);
  }


  // feed forward
  output_t operator()(const input_t& in)
  {
    return do_feed_forward<0>(in);
  }


  // feed forward
  // e.g. mynetwork(1,0,0,1);
  template <typename ... INPUT>
  output_t operator()(INPUT... in)
  {
    static_assert(sizeof...(INPUT) == input_size, "NetworkImpl::operator(): illegal input pack");
	  return do_feed_forward<0>({value_type(in)...});
  }


private:
  template <size_t I>
  auto do_feed_forward(const std::array<value_type, std::tuple_element_t<I, layer_t>::input_size>& in)
    -> std::enable_if_t<(I == output_layer), output_t>
  {
    return std::tuple_element_t<I, layer_t>::template feed<detail::accum_state_ofs<I, layer_t>::value>(in, state_.data());
  }


  template <size_t I>
  auto do_feed_forward(const std::array<value_type, std::tuple_element_t<I, layer_t>::input_size>& in)
    -> std::enable_if_t<(I < output_layer), output_t>
  {
    return do_feed_forward<I + 1>(std::tuple_element_t<I, layer_t>::template feed<detail::accum_state_ofs<I, layer_t>::value>(in, state_.data()));
  }


  std::array<value_type, state_size> state_;
};


namespace detail {

  
  template <size_t I, size_t J, typename network_t, typename Visitor>
  auto do_visit_neuron(network_t& network, Visitor&& visitor)
    -> std::enable_if_t<J == std::tuple_element_t<I, typename network_t::layer_t>::size>
  {
  }


  template <size_t I, size_t J, typename network_t, typename Visitor>
  auto do_visit_neuron(network_t& network, Visitor&& visitor)
    -> std::enable_if_t<J < std::tuple_element_t<I, typename network_t::layer_t>::size>
  {
    auto n = network. template get_neuron<I, J>();
    using node_t = decltype(n.first);
#if defined(_MSC_VER) && _MSC_VER <= 1900 && !defined(__c2__)
    visitor.operator()<node_t>(n.second, I, J);
#else
    visitor.template operator()<node_t>(n.second, I, J);
#endif
    do_visit_neuron<I, J + 1>(network, std::forward<Visitor>(visitor));
  }


  template <size_t I, typename network_t, typename Visitor>
  auto do_visit_layer(network_t& network, Visitor&& visitor)
    -> std::enable_if_t<I == network_t::output_layer>
  {
    do_visit_neuron<I, 0>(network, std::forward<Visitor>(visitor));
  }


  template <size_t I, typename network_t, typename Visitor>
  auto do_visit_layer(network_t& network, Visitor&& visitor)
    -> std::enable_if_t<I < network_t::output_layer>
  {
    do_visit_neuron<I, 0>(network, std::forward<Visitor>(visitor));
    do_visit_layer<I + 1, network_t, Visitor>(network, std::forward<Visitor>(visitor));
  }


}


// Apply visitor to all neurons in the network.
// The visitor shall be a callable c++ object with the following 
// signature:
//
// template <typename Neuron, typename T>
// void operator()(T* state, size_t layer, size_t node)
//
template <typename network_t, typename Visitor>
void visit_neurons(network_t& network, Visitor&& visitor)
{
  detail::do_visit_layer<0>(network, std::forward<Visitor>(visitor));
}


}

#endif
