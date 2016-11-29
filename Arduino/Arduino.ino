#define PARAM_LEARN_SIZE 8192 //The number of points to use in parameter learning

#ifdef NO_ARDUINO
#include "arduinocompat.h"
#endif


#define SAMPLE_RATE 1000
#define MIN_HEARTRATE 20 //BPM
#define MAX_HEARTRATE 250 //BPM

// The we expect two rising, falling edges per beat, but the 3 gives us padding
#define MAX_EDGES (PARAM_LEARN_SIZE * 3 * MAX_HEARTRATE) / (SAMPLE_RATE * 60)

#if FFT
	#define FHT_N PARAM_LEARN_SIZE

	#include <FHT.h>
	//need data to get inserted here as fht_input[]
	short fht_lin_out[NHT_N/2];
#endif

#include "data.h"
#include "constants.h"
#include "detections.h"

short ldata[PARAM_LEARN_SIZE];
char thresholded[PARAM_LEARN_SIZE];
short rising_edges[MAX_EDGES];
short falling_edges[MAX_EDGES];

short FindHeartRate();
short CountPeaks(char* th_data, short min_length, short* rising_edges, short* falling_edges, short length_data, short max_edges);
void GuessParameters2();
char one_chamber_peak_finder(short data);

struct thresholds { short low_threshold; short high_threshold;};
// Runs Binary Search on both data and -data.
static struct thresholds TryPlusMinus(short v_or_a_length, byte* multiplier) ;
static struct thresholds BinarySearch(short* data, short length, short minlen);

//Struct that holds vc, ac, and SamplesHB
struct  {
  short ac;
  short vc;
  short samplesHB;
} guessed_param;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(13, OUTPUT);
}
bool runn = true;
void loop() {
  if (runn) {
    Serial.println("Starting");
    digitalWrite(13, HIGH);
    GuessParameters2();
    digitalWrite(13, LOW);
    Serial.println(guessed_param.ac);
    Serial.println(guessed_param.vc);
  
    for (int i = 0; i < 60000; i++) {
      short val = pgm_read_word_near(data + i);
      char c = one_chamber_peak_finder(val);
      if (c != 0) {
        Serial.print(i);
        Serial.print(" ");
        Serial.println(c);
      }
    }
    Serial.println("AYYY LMAO");
  }
  runn = false;
}

static short v_thresh;
static short a_thresh;
static short num_v;
static short num_a;

char one_chamber_peak_finder(short data)
{
  if(data > guessed_param.vc)
  {
    num_a = 0;
    return ((++num_v == 8) ? 'v' : 0);
  }
  else if (data > guessed_param.ac)
  {
    num_v = 0;
    return ((++num_a == 12) ? 'a' : 0);

  }
  else
  {
    num_v = 0;
    num_a = 0;
    return 0;
  } 
}

// When it returns, the data is negated or not to match the thresholds it returns
static struct thresholds TryPlusMinus(short v_or_a_length, byte* multiplier) { // return whether or not we multiplied it as well
  struct thresholds o_cutoffs = BinarySearch(ldata, PARAM_LEARN_SIZE, v_or_a_length);

  for (short i = 0; i < PARAM_LEARN_SIZE; i++)
  	ldata[i] *= -1;
  struct thresholds o_cutoffs_neg = BinarySearch(ldata, PARAM_LEARN_SIZE, v_or_a_length);

  if (o_cutoffs_neg.high_threshold - o_cutoffs_neg.low_threshold < o_cutoffs.high_threshold - o_cutoffs.low_threshold) {
	// original data was correct, so un-negate the data
	  for (short i = 0; i < PARAM_LEARN_SIZE; i++)
		ldata[i] *= -1;
  	*multiplier = 1;
	return o_cutoffs;
  }
  else {
    *multiplier = -1;
	return o_cutoffs_neg;
  }
}


void GuessParameters2() { 
  for (int i = 0; i < PARAM_LEARN_SIZE; i++) {
    ldata[i] = pgm_read_word_near(data + i);
  }
  
  short heartbeats = FindHeartRate();
  guessed_param.samplesHB = PARAM_LEARN_SIZE/heartbeats;


  byte v_flip;
  struct thresholds v_cutoffs = TryPlusMinus(V_LENGTH, &v_flip);

  guessed_param.vc = v_cutoffs.low_threshold/2 + v_cutoffs.high_threshold/2; //.5*v_cutoffs[1] + .5*v_cutoffs[2] 


  short t_blank = 50;
  short l_blank = 50;

  // ldata already has the vflip multiplied in at this point
  for (short i = 0; i < PARAM_LEARN_SIZE; i++)
  	thresholded[i] = ldata[i] > guessed_param.vc;

  short n_edges = CountPeaks(thresholded, V_LENGTH, rising_edges, falling_edges, PARAM_LEARN_SIZE, MAX_EDGES);
  for (short peak = 0; peak < n_edges && rising_edges[peak] != -1; peak++)
  {
  	short start_idx = max(rising_edges[peak] - t_blank, 0);
	  short end_idx = min(falling_edges[peak] + l_blank, PARAM_LEARN_SIZE - 1);
	  for (short j = start_idx; j < end_idx; j++)
		  ldata[j] = 0;
  }


  byte a_flip;
  struct thresholds a_cutoffs = TryPlusMinus(A_LENGTH, &a_flip);
  // a_flip is relative to v_flip at this point. Make it absolute
  a_flip *= v_flip;
  guessed_param.ac = (a_cutoffs.low_threshold * 7 + a_cutoffs.high_threshold * 3)/10;
}
// Number of points to use in each round of flat finding
#define N_BS_PTS 12
short ths[N_BS_PTS];
short beats[N_BS_PTS];
short derivs[N_BS_PTS-1];

#define NUM_RECURSIVE_SUBDIV 3
struct thresholds NO_TH_FOUND = {-1, -1};

struct flat { short start_index; short length;};

// above_th is basically scratch memory used by this function
// both data and above_th should be of the specified length 
static struct thresholds BinarySearch(short* data, short length, short minlen) {
	short min_beats = length * MIN_HEARTRATE / (SAMPLE_RATE * 60);
	short max_beats = length * MAX_HEARTRATE / (SAMPLE_RATE * 60);

	short min_th = 0, max_th = 0;
	for (short k = 0; k < length; k++)
	{
		max_th = max(max_th, data[k]);
	}
	short next_max_th = max_th;
	for (byte i = 4; i >= 2; i /= 2)
	{
		short th = max_th / i;
		for (short k = 0; k < length; k++)
		{
			thresholded[k] = data[k] > th;
		}

		short beats = CountPeaks(thresholded, minlen, rising_edges, falling_edges, PARAM_LEARN_SIZE, MAX_EDGES);
		if (beats < min_beats)
			next_max_th = max_th;
	}
	max_th = next_max_th;



	for (byte i = 0; i < NUM_RECURSIVE_SUBDIV; i++)
	{

		// ths = linspace(min_th, max_th, npts);
		short delta = (max_th - min_th)/(N_BS_PTS - 1);
		short last = min_th;
		// rounding??
		for (int i = 0; i < N_BS_PTS; i++)
		{
			ths[i] = last;
			last += delta;
		}

		for (short j = 0; j < N_BS_PTS; j++)
		{
			for (short k = 0; k < length; k++)
			{
				thresholded[k] = data[k] > ths[j];
			}
			beats[j] = CountPeaks(thresholded, minlen, rising_edges, falling_edges, PARAM_LEARN_SIZE, MAX_EDGES);
		}
		// both indices are inclusive
		short first_valid = 0;
		while (first_valid < N_BS_PTS && ths[first_valid] < min_beats)
			first_valid++;
		short last_valid = N_BS_PTS - 1;
		while (last_valid >= 0 && ths[last_valid] > max_beats)
			last_valid++;

		if (first_valid > last_valid)
			return NO_TH_FOUND;


		short minderiv = -1;
		short minderiv_idx = -1;

		if (first_valid == last_valid)
			minderiv_idx = first_valid;

		for (short j = first_valid; j < last_valid; j++)
		{
			derivs[j] = abs(beats[j+1] - beats[j]);
			if (minderiv_idx == -1 || derivs[j] < minderiv)
			{
				minderiv = derivs[j];
				minderiv_idx = j;
			}
		}
		#if DEBUG
		Serial.print("On recursive iteration ");
		Serial.print((int)i);
		Serial.print(". Min derivative was ");
		Serial.println(minderiv);
		#endif

		short nmin, nmax; // The index of the new min, max thresholds (inclusive)
		if (minderiv == 0)
		{
			struct flat current = {-1, -1};
			struct flat best = {-1, -1};
			for (short j = first_valid; j < last_valid; j++)
			{
				if (derivs[j] == 0)
				{
					if (current.start_index == -1)
					{
						current.start_index = j;
						current.length = 0;
					}
					current.length++;
					if (current.length > best.length)
						current = best;
				}
				else
				{
					current.start_index = -1;
					current.length = -1;
				}
			}
			nmin = best.start_index - 1;
			nmax = best.start_index + best.length;
		}
		else
		{
			nmin = minderiv_idx - 1;
			nmax = minderiv_idx + 1;
		}
		// make sure they are in bounds
		if (nmin < 0)
			nmin = 0;
		if (nmax > N_BS_PTS - 1)
			nmax = N_BS_PTS - 1;

		min_th = ths[nmin];
		max_th = ths[nmax];

		if (beats[nmin] == beats[nmax])
			break;

	}
	struct thresholds toreturn =  {min_th, max_th};
	return toreturn;
}



#define RISING_EDGE 1
#define FALLING_EDGE 2


// Used to punish when the threshold is too low so large blocks of the data are above the threshold
// This is sample-rate dependent. It was 150 in the Matlab code, but dividing by 128 can be done with
// a bitshift instead of a division
#define SAMPLES_PER_ADDTL_HEARTBEAT 128
// over 1/inverse_filter_threshold samples in min_length need to be high for it to count as a peak
#define INVERSE_FILTER_THRESHOLD 4

// th_data: 1 if data[i] > current threshold
// fills in rising_edges and falling_edges with the indices of the edge
// The same number of elements should be set in both arrays, and it should be the value that is returned unless there are some peaks that are way too big.
// Unset elements will have value -1.
// all arrays should be allocated and of size length
short CountPeaks(char* th_data, short min_length, short* rising_edges, short* falling_edges, short length_data, short max_edges)
{
  bool currentState = false;
  short risingIndex = 0;
  short fallingIndex = 0;
  short peaks = 0;
  for(short i = 0; i < length_data; i++)
  {
	short acc = 0;
  	for (short j = max(0, i - length_data); j <= i && acc <= min_length/ INVERSE_FILTER_THRESHOLD; j++)
		acc += th_data[j] ? 1 : 0;
	bool isHigh = acc > min_length/INVERSE_FILTER_THRESHOLD;
	if (isHigh && !currentState && risingIndex < MAX_EDGES)
		//rising edge
		rising_edges[risingIndex++] = i;
	if (!isHigh && currentState && fallingIndex < MAX_EDGES) {
		// falling edge
		falling_edges[fallingIndex++] = i;
		peaks++;
		peaks += (falling_edges[fallingIndex - 1] - rising_edges[risingIndex - 1])/SAMPLES_PER_ADDTL_HEARTBEAT;
	}
	
	currentState = isHigh;

  }
  return peaks;
}

// Returns the expected number of beats in data
short FindHeartRate() {
#if FFT
	short sample_rate = 1000;
	//Use FHT library. Assumes the data points are in SRAM and the final output is kept in fht_input[], 
	//with the 0 -> N/2 bins having the sum of the real and imaginary components, 
	//and the N/2 -> N having the difference between the real and imaginary components. 
	//these two parts need to be squared and summed to get the overall magnitude at a particular frequency. 
	//as a result, you will have to run one of the magnitude functions to get useful data from these bins.
	fht_run();
	//This gives the magnitude of each bin in the FHT. It sums the squares of the imaginary and real, 
	//and then takes the square root. It uses a lookup table to calculate the square root, so it has limited precision. 
	//You can think of it as an 8b value times a 4b exponent. It covers the full 16b range, 
	//but only has 8b of precision at any point in that range. The data is taken in from fht_input[] and returned in fht_lin_out[]. 
	//The values are in sequential order, and there are only N/2 values total, as the FHT returns redundant data.
	fht_mag_lin();
	short data_length = sizeof(fht_input) / sizeof(short);
	short rescale = data_length / (sample_rate * 60);
	short min_idx = MIN_HEARTRATE * rescale;
	short max_idx = MAX_HEARTRATE * rescale;

	//Find index of max value of heartbeat data
	short hr_idx = 0;
	short hr_val = fht_lin_out[hr_idx];
	for (i = 0; i < (sizeof(fht_lin_out) / sizeof(short)); ++i)
	{
		if (fht_lin_out[i] > hr_val)
		{
			hr_val = fht_lin_out[i];
			hr_idx = i;
		}
	}
	hr_idx = hr_idx - 1 + min_idx;
	short heartbeats = hr_idx;
	return heartbeats;
#else
	const int HARDCODED_BPM = 100;
	return PARAM_LEARN_SIZE * HARDCODED_BPM / (60* SAMPLE_RATE );
#endif

}

