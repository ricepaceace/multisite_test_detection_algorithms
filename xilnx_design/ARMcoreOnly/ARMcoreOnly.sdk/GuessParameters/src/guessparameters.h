#include "constants.h"

static short ldata[PARAM_LEARN_SIZE];
static char thresholded[PARAM_LEARN_SIZE];
static short rising_edges[MAX_EDGES];
static short falling_edges[MAX_EDGES];
static const short data[4][10000] = {
  {
    #include "good-ch2.h"
  },
    {
    #include "good-ch3.h"
  },
    {
    #include "good-ch4.h"
  },
    {
    #include "good-ch4.h"
  }
};

struct thresholds
{
	short low_threshold;
	short high_threshold;
};

static struct thresholds BinarySearch(short* data, short data_len, short minlen);
short CountPeaks(char* th_data, short min_length, short* rising_edges, short* falling_edges, short length_data, short max_edges);
static struct thresholds TryPlusMinus(short v_or_a_length, int *multiplier);

void GuessParameters(unsigned char channel, int * v_thresh, int * v_flip, int * a_thresh, int * a_flip)
{ 
	if(channel > 3)
		return;
	int i, j;
	for (i = 0; i < PARAM_LEARN_SIZE; i++)
  	{
		ldata[i] = data[channel][i];

  	}

  	int v_flip1;
  	struct thresholds v_cutoffs = TryPlusMinus((short)V_LENGTH, &v_flip1);

  	*v_thresh = (int)(v_cutoffs.low_threshold/2 + v_cutoffs.high_threshold/2); //.5*v_cutoffs[1] + .5*v_cutoffs[2]
  	*v_flip = v_flip1;


  	// ldata already has the vflip multiplied in at this point
  	for (i = 0; i < PARAM_LEARN_SIZE; i++)
    	thresholded[i] = ldata[i] > *v_thresh;

  	short n_edges = CountPeaks(thresholded, V_LENGTH, rising_edges, falling_edges, PARAM_LEARN_SIZE, MAX_EDGES);
  	for (i = 0; i < n_edges && rising_edges[i] != -1; i++)
  	{
    	short start_idx = max(rising_edges[i] - T_BLANK, 0);
    	short end_idx = min(falling_edges[i] + L_BLANK, PARAM_LEARN_SIZE - 1);
    	for (j = start_idx; j <= end_idx; j++)
      	ldata[j] = 0;
  	}


  	int a_flip1;
 	struct thresholds a_cutoffs = TryPlusMinus(A_LENGTH, &a_flip1);
  	// a_flip is relative to v_flip at this point. Make it absolute
  	a_flip1 *= v_flip1;
  	*a_thresh = (int)((a_cutoffs.low_threshold * 7 + a_cutoffs.high_threshold * 3)/10);
  	*a_flip = a_flip1;
}

short ths[N_BS_PTS];
short beats[N_BS_PTS];
short derivs[N_BS_PTS-1];

struct thresholds NO_TH_FOUND = {-1, -1};

struct flat { short start_index; short data_len;};

// Computes the thresholds that correspond to a flat piece on the graph of threshold and counted peaks
// data should be of the specified length 
// minlen is the minimum length to call a peak (where this notion is a bit fuzzy and you should see CountPeaks for the exact definition)

static struct thresholds BinarySearch(short* data, short data_len, short minlen) {
  short min_beats = data_len / MINHEARTRATE_DIV_SAMPLRATE_60;
  short max_beats = data_len / MAXHEARTRATE_DIV_SAMPLERATE_60;
  
	short min_th = 0, max_th = 0, k, i,j;
 
	for (k = 0; k < data_len; k++)
	{
		max_th = max(max_th, data[k]);
	}
  	for (i = 0; i < NUM_RECURSIVE_SUBDIV; i++)
  	{ 
    	// ths = linspace(min_th, max_th, npts);
    	short delta = (max_th - min_th)/(N_BS_PTS - 1);
    	short last = min_th;
    	// rounding??
    	for (k = 0; k < N_BS_PTS; k++)
    	{
    	  	ths[k] = last;
      		last += delta;
    	}

		for (j = 0; j < N_BS_PTS; j++)
		{
			for (k = 0; k < data_len; k++)
			{
				thresholded[k] = data[k] > ths[j];
			}
			beats[j] = CountPeaks(thresholded, minlen, rising_edges, falling_edges, PARAM_LEARN_SIZE, MAX_EDGES); 
		}

		// both indices are inclusive
		short first_valid = 0;
		while (first_valid < N_BS_PTS && beats[first_valid] > max_beats)
			first_valid++;
      
		short last_valid = N_BS_PTS - 1;
		while (last_valid >= 0 && beats[last_valid] < min_beats)
			last_valid--;
    
		if (first_valid > last_valid)
		{
			return NO_TH_FOUND;
		}
    
		short minderiv = -1;
		short minderiv_idx = -1;

		if (first_valid == last_valid)
			minderiv_idx = first_valid;

		for (j = first_valid; j < last_valid; j++)
		{
		  	derivs[j] = abs(beats[j+1] - beats[j]);
		  	if (minderiv_idx == -1 || derivs[j] < minderiv)
		  	{
		    	minderiv = derivs[j];
		    	minderiv_idx = j;
		  	}
		}

    	short nmin, nmax; // The index of the new min, max thresholds (inclusive)
    	if (minderiv == 0)
    	{
      		struct flat current = {-1, -1};
      		struct flat best = {-1, -1};
      		for (j = first_valid; j < last_valid; j++)
      		{
        		if (derivs[j] == 0)
        		{
          			if (current.start_index == -1)
          			{
            			current.start_index = j;
            			current.data_len = 0;
          			}
          			current.data_len++;
          			if (current.data_len > best.data_len)
            			best = current;
    			  }
        		else
        		{
         	 		current.start_index = -1;
          	  current.data_len = -1;
    			  }
  			  }
          
      		nmin = best.start_index - 1;
      		nmax = best.start_index + best.data_len;
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

short CountPeaks(char* th_data, short min_length, short* rising_edges, short* falling_edges, short length_data, short max_edges)
{
  short i,j,k;
  short currentState = 0;
  short risingIndex = 0;
  short fallingIndex = 0;
  short peaks = 0;
  for (k = 0; k < MAX_EDGES; k++) {
    rising_edges[k] = -1;
    falling_edges[k] = -1;
  }

  short filter_thresh =  min_length/ INVERSE_FILTER_THRESHOLD;
  for(i = 0; i < length_data; i++)
  {
    short acc = 0;


    for (j=max(0, i - min_length); j <= i && acc <= filter_thresh; j++)
      acc += th_data[j] ? 1 : 0;

    short isHigh = acc > filter_thresh;
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

// When it returns, the data is negated or not to match the thresholds it returns
static struct thresholds TryPlusMinus(short v_or_a_length, int *multiplier) // return whether or not we multiplied it as well
{

	struct thresholds o_cutoffs = BinarySearch(ldata, PARAM_LEARN_SIZE, v_or_a_length);
	short i;
  	for (i = 0; i < PARAM_LEARN_SIZE; i++)
  	{
    	ldata[i] *= -1;
    }

  	struct thresholds o_cutoffs_neg = BinarySearch(ldata, PARAM_LEARN_SIZE, v_or_a_length);

  	if (o_cutoffs_neg.high_threshold - o_cutoffs_neg.low_threshold < o_cutoffs.high_threshold - o_cutoffs.low_threshold)
  	{
		// original data was correct, so un-negate the data
		for (i = 0; i < PARAM_LEARN_SIZE; i++)
		{
			ldata[i] *= -1;
		}
    	*multiplier = 1;
    	return o_cutoffs;
  	}
  	else
  	{
    	*multiplier = -1;
    	return o_cutoffs_neg;
  	}
}
