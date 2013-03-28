/* -*- c++ -*- */
/*
 * Copyright (C) 2013 Alexandru Csete, OZ9AEC
 *
 * Gqrx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * Gqrx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Gqrx; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#ifndef RECEIVER_H
#define RECEIVER_H

// standard includes
#include <string>

// GNU Radio includes
#include <analog/quadrature_demod_cf.h>
#include <blocks/file_sink.h>
#include <blocks/file_source.h>
#include <blocks/null_sink.h>
#include <blocks/sub_ff.h>
#include <blocks/throttle.h>
#include <digital/clock_recovery_mm_ff.h>
#include <filter/fft_filter_ccc.h>
#include <filter/firdes.h>
#include <filter/single_pole_iir_filter_ff.h>
#include <gr_complex.h>
#include <gr_top_block.h>


/*! \defgroup RX High level receiver blocks. */

/*! \brief Top-level receiver class.
 *  \ingroup RX
 *
 * This class encapsulates the GNU Radio flow graph for the receiver.
 * Front-ends should only control the receiver through the interface provided
 * by this class.
 *
 */
class receiver
{

public:

    receiver(const std::string input_device="", const std::string audio_device="", double quad_rate=2.e6);
    ~receiver();

    void start();
    void stop();

    void set_input_device(const std::string device);
    void set_output_device(const std::string device);

    void set_rf_freq(double freq_hz);
    double rf_freq(void);
    void rf_range(double *start, double *stop, double *step);

    void set_rf_gain(double gain_rel);
    double rf_gain(void);

    void set_filter(double low, double high, double trans_width);

private:
    void connect_all(void);

private:
    gr_top_block_sptr tb;  /*!< Receiver top block. */
    
    gr::blocks::file_source::sptr    src;
    gr::blocks::throttle::sptr       throttle;
    std::vector<gr_complex>          taps;
    gr::filter::fft_filter_ccc::sptr filter;
    gr::analog::quadrature_demod_cf::sptr demod;
    gr::filter::single_pole_iir_filter_ff::sptr iir;
    gr::blocks::sub_ff::sptr sub;
    
    gr::digital::clock_recovery_mm_ff::sptr clock_recov;
    gr::blocks::file_sink::sptr fifo;

    bool d_running;
    double d_quad_rate;

};

#endif // RECEIVER_H
