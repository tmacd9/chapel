//
// Bounded Buffer Exercise
//
// Create a bounded buffer to manage pipeline parallelism
//

config const problemSize: int = 10;

class BoundedBuffer {
  param bufSize:int = 2;
  type eltType = int;

  var buffer$: [0..#bufSize] sync eltType;
  var producerPos$: sync int = 0;
  var consumerPos$: sync int = 0;

  //
  // Add a value to the circular buffer. If it is full, wait until a
  // value has been consumed
  //
  def add(i: eltType) {
    var c = producerPos$;
    producerPos$ = (c + 1) % bufSize;
    buffer$(c) = i;
  }

  //
  // Remove a value from the buffer. If it is empty, wait until a
  // value has been produced
  //
  def remove(): eltType {
    var c = consumerPos$;
    consumerPos$ = (c + 1) % bufSize;
    return buffer$(c);
  }

  // 
  // Yield all values in the buffer until the -1 sentinel value is found
  //
  def these() {
    var val = remove();
    while val != -1 {
      yield val;
      val = remove();
    }
    add(-1);
  }
}

var buffer: BoundedBuffer = new BoundedBuffer();

// Given a value, do some work on it to create the next value. In this
// case, the work is sleeping for a second and leaving the value unchanged.
def createNextValue(val) {
  use Time;
  sleep(1);
  return val;
}


// Produce the numbers from 1 to nProducts by adding them to the
// bounded buffer. -1 is used as a sentinel to indicate that the final
// value has been produced.
def producer(nProducts: int) {
  for i in 1..nProducts {
    createNextValue(i);
    writeln("producer producing ", i);
    buffer.add(i);
  }
  writeln("producer producing ", -1);
  buffer.add(-1);
}


// Consume values from the buffer. These values were created by the producer.
def consumer() {
  for consumedValue in buffer {
    writeln("consumer consumed: ", consumedValue);
  }
}

def main {
  // Simultaneously start a producer and a consumer.
  cobegin {
    producer(problemSize);
    consumer();
  }
}
