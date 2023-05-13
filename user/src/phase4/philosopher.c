#include "philosopher.h"

// TODO: define some sem if you need
int mutex;
int chopstick[PHI_NUM];

void init() {
  // init some sem if you need
  // TODO();
  mutex = sem_open(1);
  for(int i = 0; i < PHI_NUM; i++){
    chopstick[i] = sem_open(1);
  }
}

void philosopher(int id) {
  // implement philosopher, remember to call `eat` and `think`
  while (1) {
    // TODO();
    think(id);
    P(mutex);
    P(chopstick[id]);
    P(chopstick[(id+1)%PHI_NUM]);
    eat(id);
    V(chopstick[id]);
    V(chopstick[(id+1)%PHI_NUM]);
    V(mutex);
    think(id);
  }
}
