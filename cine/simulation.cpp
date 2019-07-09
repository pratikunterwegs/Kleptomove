#include <iostream>
#include <experimental/filesystem>
#include "simulation.h"
#include "game_watches.hpp"
#include "cmd_line.h"


namespace filesystem = std::experimental::filesystem;


namespace cine2 {


  Simulation::Simulation(const Param& param)
    : g_(-1), t_(-1),
    param_(param)
  {
    using Layers = Landscape::Layers;

    agents_.pop = std::vector<Individual>(param.agents.N);
    agents_.tmp_pop = std::vector<Individual>(param.agents.N);
    agents_.ann = make_any_ann(param.agents.L, param.agents.N, param.agents.ann.c_str());
    agents_.fitness = std::vector<float>(param.agents.N, 0.f);
    agents_.tmp_ann = make_any_ann(param.agents.L, param.agents.N, param.agents.ann.c_str());



    // initial landscape layers from image fies
    init_layer(param_.landscape.capacity); //capacity
    if (landscape_.dim() < 32) throw std::runtime_error("Landscape too small");

    // full grass cover
    //for (auto& g : landscape_[Layers::items]) g = param.landscape.max_grass_cover;
    const int DD = landscape_.dim() * landscape_.dim();
    float* __restrict items = landscape_[Layers::items].data();
    float* __restrict capacity = landscape_[Layers::capacity].data();
    for (int i = 0; i < DD; ++i) {

      items[i] = floor(capacity[i] * param.landscape.max_item_cap);
      //items[i] = capacity[i];

    }
    
    //empty grass cover
    //for (auto& g : landscape_[Layers::items]) g = 0.0f;

    // initial positions
    auto coorDist = std::uniform_int_distribution<short>(0, short(landscape_.dim() - 1));
    for (auto& p : agents_.pop) { p.pos.x = coorDist(rnd::reng); p.pos.y = coorDist(rnd::reng); }
    //for (auto& p : pred_.pop) { p.pos.x = coorDist(rnd::reng); p.pos.y = coorDist(rnd::reng); }

    // initial occupancies and observable densities
    landscape_.update_occupancy(Layers::foragers_count, Layers::foragers, Layers::klepts_count, Layers::klepts, Layers::handlers_count, Layers::handlers, agents_.pop.cbegin(), agents_.pop.cend(), param_.landscape.foragers_kernel);
    //landscape_.update_occupancy(Layers::pred_count, Layers::pred, pred_.pop.cbegin(), pred_.pop.cend(), param_.landscape.pred_kernel);

    // optional: initialization from former runs
    if (!param_.init_agents_ann.empty()) {
      init_anns_from_archive(agents_, archive::iarch(param_.init_agents_ann));
    }
    //if (!param_.init_pred_ann.empty()) {
    //  init_anns_from_archive(pred_, archive::iarch(param_.init_pred_ann));
    //}
  }


  void Simulation::init_anns_from_archive(Population& Pop, archive::iarch& ia)
  {
    auto cm = ia.extract(param_.initG >= 0 ? std::min(param_.initG, param_.G - 1) : param_.G - 1);
    if (cm.un != Pop.ann->N()) throw cmd::parse_error("Number of ANNs doesn't match");
    if (cm.usize != Pop.ann->type_size()) throw cmd::parse_error("ANN state size doesn't match");
    auto dst = Pop.ann->data();
    uncompress(dst, cm, Pop.ann->stride() * sizeof(float));
  }


  namespace detail {


    template <typename FITNESSFUN>
    void assess_fitness(Population& population,
      const Param::ind_param& iparam,
      FITNESSFUN fitness_fun)
    {
      const float cmplx_penalty = iparam.cmplx_penalty;
      const auto& pop = population.pop;
      const auto& ann = population.ann;
      auto& fitness = population.fitness;
      const int N = static_cast<int>(pop.size());
#     pragma omp parallel for schedule(static)
      for (int i = 0; i < N; ++i) {
        fitness[i] = fitness_fun(pop[i], ann->complexity(i), cmplx_penalty);
      }
      population.rdist.mutate(fitness.cbegin(), fitness.cend());
    }


    void create_new_generation(const Landscape& landscape,
      Population& population,
      const Param::ind_param& iparam,
      bool fixed)
    {
      const auto& pop = population.pop;
      const auto& ann = *population.ann;
      const int N = static_cast<int>(pop.size());
#     pragma omp parallel 
      {
        auto& tmp_pop = population.tmp_pop;
        auto& tmp_ann = *population.tmp_ann;
        const auto& rdist = population.rdist;
        const auto coorDist = rndutils::uniform_signed_distribution<short>(-iparam.sprout_radius, iparam.sprout_radius);
#       pragma omp for schedule(static)
        for (int i = 0; i < N; ++i) {
          const int ancestor = rdist(rnd::reng);
          auto newPos = pop[ancestor].pos + Coordinate{ coorDist(rnd::reng), coorDist(rnd::reng) };
          tmp_pop[i].sprout(landscape.wrap(newPos), ancestor);
          tmp_ann.assign(ann, ancestor, i);   // copy ann
        }
      }
      population.tmp_ann->mutate(iparam, fixed);

      using std::swap;
      swap(population.pop, population.tmp_pop);
      swap(population.ann, population.tmp_ann);
    }

  }


#define simulation_observer_notify(msg) \
  if ((observer ? !observer->notify(this, msg) : true)) return false


  bool Simulation::run(Observer* observer)
  {
    // burn-in
    simulation_observer_notify(INITIALIZED);
    const int Gb = param_.Gburnin;
    for (int gb = 0; gb < Gb; ++gb) {
      const int Tb = param_.T;
      for (int tb = 0; tb < Tb; ++tb) {
        simulate_timestep();
        simulation_observer_notify(WATCHDOG);   // app alive?
      }

      //assess_fitness(); //CN: fix?
      // clear fitness
      agents_.fitness.assign(agents_.fitness.size(), 0.f);
    
      assess_fitness(); //CN: fix?
      create_new_generations();
    }
    const int G = param_.G;
    for (g_ = 0; g_ < G; ++g_) {
      simulation_observer_notify(NEW_GENERATION);
      const int T = fixed() ? param_.Tfix : param_.T;
      for (t_ = 0; t_ < T; ++t_) {
        simulate_timestep();
        simulation_observer_notify(POST_TIMESTEP);
        // to print one screenshot
        /*
        if (g_ == 50 && t_ == 25) {
          Image screenshot2(std::string("../settings/screenshot.png"));

          layer_to_image_channel(screenshot2, landscape_[Landscape::Layers::agents_count], blue);
          layer_to_image_channel(screenshot2, landscape_[Landscape::Layers::pred_count], red);
          layer_to_image_channel(screenshot2, landscape_[Landscape::Layers::grass], green);
          save_image(screenshot2, std::string("../settings/screenshot.png"));
        }
        */
        //to print one screenshot end
      }



      assess_fitness();
      analysis_.generation(this);
      simulation_observer_notify(GENERATION);
      create_new_generations();
    }
    simulation_observer_notify(FINISHED);
    return true;
  }

#undef simulation_observer_notify


  void Simulation::simulate_timestep()
  {
    using Layers = Landscape::Layers;

    // grass growth
    const int DD = landscape_.dim() * landscape_.dim();
    float* __restrict items = landscape_[Layers::items].data();					//items now refers to the layer of food items (in landscape)
    float* __restrict capacity = landscape_[Layers::capacity].data();			//capacity refers to the maximum capacity layer (in landscape)
    ann_assume_aligned(items, 32);
    const float max_item_cap = param_.landscape.max_item_cap;
    const float item_growth = param_.landscape.item_growth;
    //#   pragma omp parallel for schedule(static)

    for (int i = 0; i < DD; ++i) {
          if (std::bernoulli_distribution(item_growth)(rnd::reng)) {  // altered: probability that items drop
            //items[i] = std::min(floor(capacity[i] * max_item_cap), items[i] + 1.0f);
            items[i] = std::min(floor(capacity[i] * max_item_cap), floor(items[i] + 1.0f));
            //items[i] = std::min(capacity[i], items[i] + 1.0f/ max_item_cap);
          }
        }
    
    auto last_agents = agents_.pop.data() + agents_.pop.size();
    for (auto agents = agents_.pop.data(); agents != last_agents; ++agents) {
      agents->do_handle();
    }

    landscape_.update_occupancy(Layers::foragers_count, Layers::foragers, Layers::klepts_count, Layers::klepts, Layers::handlers_count, Layers::handlers, agents_.pop.cbegin(), agents_.pop.cend(), param_.landscape.foragers_kernel);

    // move
    agents_.ann->move(landscape_, agents_.pop, param_.agents);

  

    // update occupancies and observable densities

	//RESOLVE GRAZING AND ATTACK function!
    resolve_grazing_and_attacks();


    landscape_.update_occupancy(Layers::foragers_count, Layers::foragers, Layers::klepts_count, Layers::klepts, Layers::handlers_count, Layers::handlers, agents_.pop.cbegin(), agents_.pop.cend(), param_.landscape.foragers_kernel);

  }


  void Simulation::assess_fitness()
  {
    detail::assess_fitness(agents_, param_.agents, Param::agents_fitness);
  }


  void Simulation::create_new_generations()
  {
    detail::create_new_generation(landscape_, agents_, param_.agents, fixed());
  }


  void Simulation::resolve_grazing_and_attacks()
  {
    using Layers = Landscape::Layers;
    const float detection_rate = param_.landscape.detection_rate;
    LayerView foragers_count = landscape_[Layers::foragers_count];
    LayerView klepts_count = landscape_[Layers::klepts_count];
    LayerView capacity = landscape_[Layers::capacity];
    LayerView items = landscape_[Layers::items];
    LayerView handlers = landscape_[Layers::handlers];
    LayerView old_grass = landscape_[Layers::temp];
    old_grass.copy(handlers);

    attacking_inds_.clear();
    attacked_potentially_.clear();
    attacked_inds.clear();
    auto last_agents = agents_.pop.data() + agents_.pop.size();
    for (auto agents = agents_.pop.data(); agents != last_agents; ++agents) {
      if (agents->handle() == false) {
        const Coordinate pos = agents->pos;

        if (agents->foraging) {
          if (items(pos) >= 1.0f){
            if (std::bernoulli_distribution(1.0 - pow((1.0f - detection_rate), items(pos)))(rnd::reng)) { // Ind searching for items
              agents->pick_item();
              items(pos) -= 1.0f;

            }
          }
        }
      }
    }

    for (int i = 0; i < agents_.pop.size(); ++i) {
      if (!agents_.pop[i].handling && !agents_.pop[i].foraging) {

        const Coordinate pos = agents_.pop[i].pos;
        if (handlers(pos) >= 1.0f) {
          attacking_inds_.push_back(i);

        }
      }
    }

    for (auto i : attacking_inds_) {						//cycle through the agents in that same vector

      for (auto attacked_pot = agents_.pop.data(); attacked_pot != last_agents; ++attacked_pot) {
        const Coordinate pos = attacked_pot->pos;
        if (agents_.pop[i].pos == pos && &agents_.pop[i] != attacked_pot) {  // self excluded
          if (attacked_pot->handling) {
            attacked_potentially_.push_back(attacked_pot);

          }
        }
      }
      if (!attacked_potentially_.empty()) {								//if then that vector is NOT empty
        std::uniform_int_distribution<int> rind(0, static_cast<int>(attacked_potentially_.size() - 1));		//sample one (random)
        int focal_ind = rind(rnd::reng);																	//now called "focal_ind"
        attacked_inds.push_back(attacked_potentially_[focal_ind]);			//added to the vector of ACTUALLY ATTACKED.

        attacked_potentially_.clear();										//clearing the POTENTIALLY ATTACKED vector
      }

    }

    // Shuffling
    std::vector<std::pair<int, Individual*>> conflicts_v(attacking_inds_.size());
    for (int i = 0; i < attacking_inds_.size(); ++i) {
      conflicts_v[i] = { attacking_inds_[i], attacked_inds[i] };
    }
    std::shuffle(conflicts_v.begin(), conflicts_v.end(), rnd::reng);

    for (int i = 0; i < attacking_inds_.size(); ++i) {				//cycle through the agents who attack
      float prob_to_fight = 1.0f;									//they always fight

      //if (attacked_inds[i]->handle())
      //  prob_to_fight = 0.5f;
      //else
      //  prob_to_fight = 0.2f;


      std::bernoulli_distribution fight(prob_to_fight);								//sampling whether fight occurs
      std::bernoulli_distribution initiator_wins(1.0)/*initiator always wins*/;		//sampling whether the initiator wins or not
      if (attacked_inds[i]->handling) {			///isn't this always true?
        if (fight(rnd::reng)) {
          if (initiator_wins(rnd::reng)) {
            agents_.pop[attacking_inds_[i]].handling = attacked_inds[i]->handling;
            agents_.pop[attacking_inds_[i]].handle_time = attacked_inds[i]->handle_time;
            //attacking_inds_[i]->food += 1.0f;
            attacked_inds[i]->flee(landscape_, param_.agents.flee_radius);

          }
          else
            agents_.pop[attacking_inds_[i]].flee(landscape_, param_.agents.flee_radius);
          //Energetic costs

          //attacking_inds_[i]->food -= 0.0f;
          //attacked_inds[i]->food -= 0.0f;
        }

      }
    }
    conflicts_v.clear();

  }


  void Simulation::init_layer(image_layer imla)
  {
    Image image(std::string("../settings/") + imla.image);
    if (landscape_.dim() == 0) {
      landscape_ = Landscape(image.width());
    }
    if (!(image.width() == landscape_.dim() && image.height() == landscape_.dim())) {
      throw std::runtime_error("image dimension mismatch");
    }
    image_channel_to_layer(landscape_[imla.layer], image, imla.channel);
  }


  class SimpleObserver : public Observer
  {
  public:
    SimpleObserver() {}
    ~SimpleObserver() override {}

    // required observer interface
    bool notify(void* userdata, long long msg) override
    {
      auto sim = reinterpret_cast<Simulation*>(userdata);
      using msg_type = Simulation::msg_type;
      switch (msg) {
      case msg_type::INITIALIZED:
        std::cout << "Simulation initialized\n";
        break;
      case msg_type::NEW_GENERATION: {
        watch_.reset();
        watch_.start();
        std::cout << "Generation: " << sim->generation() << (sim->fixed() ? "*  " : "   ");
        break;
      }
      case msg_type::GENERATION: {
        std::cout << sim->analysis().agents_summary().back().ave_fitness << "   ";
        std::cout << sim->analysis().agents_summary().back().repro_ind << "   ";
        std::cout << sim->analysis().agents_summary().back().repro_ann << "  (";
        std::cout << sim->analysis().agents_summary().back().complexity << ");   ";



        std::cout << (int)(1000 * watch_.elapsed().count()) << "ms\n";
        break;
      }
      case msg_type::FINISHED:
        std::cout << "\rSimulation finished\n";
        break;
      }
      return notify_next(userdata, msg);
    };

  private:
    game_watches::Stopwatch watch_;
  };


  std::unique_ptr<Observer> CreateSimpleObserver()
  {
    return std::unique_ptr<Observer>(new SimpleObserver());
  }

}
