#include <vector>
#include <string>
#include <limits>
#include <iostream>
#include <functional>
#include <csignal>

using namespace std;

const float ms = 1/1e3;
const float us = 1/1e6;
const float ns = 1/1e9;
const float MB = 1024ull*1024;
const float GB = 1024*MB;
const float TB = 1024*GB;
const float K = 1e3;
const float M = 1e6;

struct Tech {
   string name;
   float capacity;  // capacity in bytes
   float cost;      // cost in $
   float IOs;       // IO operations per second
   float latency;   // latency in seconds
   unsigned max;    // maximum device count
};

vector<Tech> techs = {
   {"RAM",  64*GB, 500,  10*M, 100*ns, 16},
   {"NVM", 256*GB, 500,   5*M, 400*ns,  8},
   {"SSD",   1*TB, 500, 500*K, 100*us, 16},
   {"HDD",   4*TB, 200,   100,  10*ms, 16}
};

struct AccessGroup {
   float fraction; // fraction of all accesses
   float size; // access size in bytes
};

typedef vector<unsigned> Config; // number of devices per Tech
typedef vector<AccessGroup> Workload; // fraction of accesses

bool isValid(Config& config, Workload& workload) {
   if (config[0]==0) // we need some RAM
      return false;
   float sumSpaceNeeded = 0;
   for (AccessGroup g : workload)
      sumSpaceNeeded += g.size;
   vector<unsigned> indexes;
   for (unsigned t=0; t<techs.size(); t++)
      if (config[t])
         indexes.push_back(t);
   for (unsigned i=1; i<indexes.size(); i++)
      if ((config[indexes[i]] * techs[indexes[i]].capacity) < (config[indexes[i-1]] * techs[indexes[i-1]].capacity))
         return false; // level is not inclusive
   if ((config[indexes.back()] * techs[indexes.back()].capacity) < sumSpaceNeeded)
      return false; // last-level device not big enough for data
   return true;
}

float configCost(Config& config) {
   float cost = 0;
   for (unsigned i=0; i<techs.size(); i++)
      cost += config[i] * techs[i].cost;
   return cost;
}

// average time per access in seconds
float avgTimePerAccess(vector<float>& accessFractions) {
   float sum = 0;
   for (unsigned i=0; i<accessFractions.size(); i++)
      sum += accessFractions[i] * (1/techs[i].IOs);
   return sum;
}

// average latency per access in seconds
float avgLatencyPerAccess(vector<float>& accessFractions) {
   float sum = 0;
   for (unsigned i=0; i<accessFractions.size(); i++)
      sum += accessFractions[i] * (techs[i].latency);
   return sum;
}

void printCapacity(vector<unsigned>& config, unsigned tech) {
   float capacity = config[tech] * techs[tech].capacity;
   if (capacity >= TB) {
      cout << capacity/TB << " TB";
   } else if (capacity >= GB) {
      cout << capacity/GB << " GB";
   } else if (capacity >= MB) {
      cout << capacity/MB << " MB";
   } else {
      cout << capacity;
   }
}

// access fraction for each technology
void computeAccessFractions(vector<AccessGroup>& workload, vector<unsigned>& config, vector<float>& fractionsOut) {
   fill(fractionsOut.begin(), fractionsOut.end(), 0);
   unsigned tech = 0;
   float techCapacity = config[tech] * techs[tech].capacity;
   for (AccessGroup g : workload) {
      while (true) {
         if (g.size > techCapacity) {
            float f = (techCapacity/g.size) * g.fraction;
            fractionsOut[tech] += f;
            g.fraction -= f;
            // g.size does not change as we assume inclusive caches
            tech++;
            techCapacity = config[tech] * techs[tech].capacity;
         } else {
            fractionsOut[tech] += g.fraction;
            techCapacity -= g.size;
            break;
         }
      }
   }
}

vector<unsigned> findBestConfig(vector<AccessGroup>& workload, float costLimit, bool optimizeThroughput) {
   vector<unsigned> config(techs.size(), 0);
   vector<float> accessFractions(config.size(), 0);
   float bestCost = numeric_limits<float>::max();
   float bestTime = numeric_limits<float>::max();
   vector<unsigned> bestConfig(techs.size(), 0);
   vector<float> bestAccessFractions(config.size(), 0);

   function<void(vector<unsigned>& config, unsigned configPos)> enumerate;
   enumerate = [&](vector<unsigned>& config, unsigned configPos) {
      if (configPos==techs.size()) {
         //if (config[0]==2 && config[1] == 1 && config[2] == 11 && config[3]== 0) raise(SIGTRAP);

         if (isValid(config, workload)) {
            float cost = configCost(config);
            if (cost<costLimit) {
               computeAccessFractions(workload, config, accessFractions);
               float time = optimizeThroughput ? avgTimePerAccess(accessFractions) : avgLatencyPerAccess(accessFractions);
               if (time<bestTime || (time==bestTime && cost<bestCost)) {
                  bestTime = time;
                  bestConfig = config;
                  bestAccessFractions = accessFractions;
                  bestCost = cost;
               }
            }
         }
         return;
      }

      for (unsigned count=0; count<techs[configPos].max; count++) {
         config[configPos] = count;
         enumerate(config, configPos+1);
      }
   };

   enumerate(config, 0);

   cout <<  "ops/s: " << 1/bestTime << " (" << (optimizeThroughput?"throughput":"latency") << ")"<< endl;
   for (unsigned t=0; t<techs.size(); t++) {
      cout << techs[t].name << " ";
      printCapacity(bestConfig, t);
      cout << " ($" << (techs[t].cost*bestConfig[t]) << ")";
      cout << ": " << bestAccessFractions[t] << endl;
   }
   cout << "totalCost: $" << bestCost << endl << endl;

   return bestConfig;
}

int main() {
   //vector<AccessGroup> workload = {{.8, 111*GB}, {.1, 1*TB}, {.1, 10*TB}};
   //vector<AccessGroup> workload = {{.8, 300*GB}, {.1, 1*TB}, {.1, 10*TB}};
   //vector<AccessGroup> workload = {{.8, 300*GB}, {.1, 2*TB}, {.1, 10*TB}};

   vector<AccessGroup> workload = {{.8, 111*GB}, {.2-.001, 1*TB}, {.001, 10*TB}};

   for (float costLimit : {2000, 4000, 6000, 8000, 10000, 15000, 100000}) {
      cout << "---" << endl << "cost budget $" << costLimit << endl;
      auto tp = findBestConfig(workload, costLimit, true);
      //auto lt = findBestConfig(workload, costLimit, false);
   }

   return 0;
}
