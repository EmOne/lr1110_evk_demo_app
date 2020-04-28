/**
 * @file      main.cpp
 *
 * @brief     LR1110 EVK application entry point.
 *
 * Revised BSD License
 * Copyright Semtech Corporation 2020. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "configuration.h"
#include "lr1110_system.h"
#include "lr1110_wifi.h"
#include "system.h"

#include "supervisor.h"
#include "environment_interface.h"
#include "log.h"
#include "antenna_selector_interface.h"
#include "signaling_interface.h"
#include "timer_interface_implementation.h"

#include "gui.h"
#include "stdio.h"
#include "string.h"
#include "demo.h"

#include "stm32_assert_template.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

radio_t radio = {
    SPI1,
    { LR1110_NSS_PORT, LR1110_NSS_PIN },
    { LR1110_RESET_PORT, LR1110_RESET_PIN },
    { LR1110_IRQ_PORT, LR1110_IRQ_PIN },
    { LR1110_BUSY_PORT, LR1110_BUSY_PIN },
};

class Environment : public EnvironmentInterface
{
   public:
    virtual time_t GetLocalTimeSeconds( ) const { return system_time_GetTicker( ) / 1000; }
    virtual time_t GetLocalTimeMilliseconds( ) const { return system_time_GetTicker( ); }
};

class AntennaSelector : public AntennaSelectorInterface
{
   public:
    AntennaSelector( ) : AntennaSelectorInterface( ) {}
    virtual void SelectAntenna1( )
    {
        AntennaSelector::init_if_not( );

        system_gpio_set_pin_state( AntennaSelector::antenna_selector_ctrl, SYSTEM_GPIO_PIN_STATE_LOW );
        system_gpio_set_pin_state( AntennaSelector::antenna_selector_n_ctrl, SYSTEM_GPIO_PIN_STATE_HIGH );
    }

    virtual void SelectAntenna2( )
    {
        AntennaSelector::init_if_not( );

        system_gpio_set_pin_state( AntennaSelector::antenna_selector_ctrl, SYSTEM_GPIO_PIN_STATE_HIGH );
        system_gpio_set_pin_state( AntennaSelector::antenna_selector_n_ctrl, SYSTEM_GPIO_PIN_STATE_LOW );
    }

   protected:
    static void init_if_not( )
    {
        if( AntennaSelector::has_been_init )
        {
            return;
        }
        else
        {
            system_gpio_init_direction_state( AntennaSelector::antenna_selector_ctrl, SYSTEM_GPIO_PIN_DIRECTION_OUTPUT,
                                              SYSTEM_GPIO_PIN_STATE_LOW );
            system_gpio_init_direction_state( AntennaSelector::antenna_selector_n_ctrl,
                                              SYSTEM_GPIO_PIN_DIRECTION_OUTPUT, SYSTEM_GPIO_PIN_STATE_LOW );

            AntennaSelector::has_been_init = true;
        }
    }

   private:
    static bool   has_been_init;
    static gpio_t antenna_selector_ctrl;
    static gpio_t antenna_selector_n_ctrl;
};
bool   AntennaSelector::has_been_init           = false;
gpio_t AntennaSelector::antenna_selector_ctrl   = { ANTENNA_SWITCH_CTRL_PORT, ANTENNA_SWITCH_CTRL_PIN };
gpio_t AntennaSelector::antenna_selector_n_ctrl = { ANTENNA_SWITCH_N_CTRL_PORT, ANTENNA_SWITCH_N_CTRL_PIN };

class Signaling : public SignalingInterface
{
   public:
    explicit Signaling( const EnvironmentInterface* environment )
        : SignalingInterface( ),
          environment( environment ),
          do_monitor_tx( false ),
          turn_on_tx_instant_ms( 0 ),
          do_monitor_rx( false ),
          turn_on_rx_instant_ms( 0 )
    {
    }
    virtual ~Signaling( ) {}

    void Runtime( )
    {
        const uint32_t now_ms = this->environment->GetLocalTimeMilliseconds( );
        if( this->do_monitor_tx )
        {
            if( ( now_ms - this->turn_on_tx_instant_ms ) > DURATION_TX_ON_MS )
            {
                this->do_monitor_tx = false;
                system_gpio_set_pin_state( Signaling::led_tx, SYSTEM_GPIO_PIN_STATE_LOW );
            }
        }
        if( this->do_monitor_rx )
        {
            if( ( now_ms - this->turn_on_rx_instant_ms ) > Signaling::DURATION_RX_ON_MS )
            {
                this->do_monitor_rx = false;
                system_gpio_set_pin_state( Signaling::led_rx, SYSTEM_GPIO_PIN_STATE_LOW );
            }
        }
    }

    virtual void StartCapture( ) { system_gpio_set_pin_state( Signaling::led_scan, SYSTEM_GPIO_PIN_STATE_HIGH ); }
    virtual void StopCapture( ) { system_gpio_set_pin_state( Signaling::led_scan, SYSTEM_GPIO_PIN_STATE_LOW ); }
    virtual void Tx( )
    {
        this->do_monitor_tx         = true;
        this->turn_on_tx_instant_ms = this->environment->GetLocalTimeMilliseconds( );
        system_gpio_set_pin_state( Signaling::led_tx, SYSTEM_GPIO_PIN_STATE_HIGH );
    }
    virtual void Rx( )
    {
        this->do_monitor_rx         = true;
        this->turn_on_rx_instant_ms = this->environment->GetLocalTimeMilliseconds( );
        system_gpio_set_pin_state( Signaling::led_rx, SYSTEM_GPIO_PIN_STATE_HIGH );
    }
    virtual void StartContinuousTx( )
    {
        this->do_monitor_tx = false;
        this->do_monitor_rx = false;
        system_gpio_set_pin_state( Signaling::led_tx, SYSTEM_GPIO_PIN_STATE_HIGH );
    }
    virtual void StopContinuousTx( )
    {
        this->do_monitor_tx = false;
        this->do_monitor_rx = false;
        system_gpio_set_pin_state( Signaling::led_tx, SYSTEM_GPIO_PIN_STATE_LOW );
    }

   protected:
    static uint32_t DURATION_TX_ON_MS;
    static uint32_t DURATION_RX_ON_MS;

   private:
    static gpio_t               led_scan;
    static gpio_t               led_tx;
    static gpio_t               led_rx;
    const EnvironmentInterface* environment;
    bool                        do_monitor_tx;
    uint32_t                    turn_on_tx_instant_ms;
    bool                        do_monitor_rx;
    uint32_t                    turn_on_rx_instant_ms;
};
gpio_t   Signaling::led_scan          = { LR1110_LED_SCAN_PORT, LR1110_LED_SCAN_PIN };
gpio_t   Signaling::led_tx            = { LR1110_LED_TX_PORT, LR1110_LED_TX_PIN };
gpio_t   Signaling::led_rx            = { LR1110_LED_RX_PORT, LR1110_LED_RX_PIN };
uint32_t Signaling::DURATION_TX_ON_MS = 100;
uint32_t Signaling::DURATION_RX_ON_MS = 100;

int main( void )
{
    bool automatic_mode = false;

    system_init( );

    system_time_wait_ms( 500 );

    lv_init( );
    lv_port_disp_init( );
    lv_port_indev_init( );

    Logging::EnableLogging( );

    Environment     environment;
    AntennaSelector antenna_selector;
    Signaling       signaling( &environment );
    Gui             gui;
    Timer           timer;
    Demo            demo( &radio, &environment, &antenna_selector, &signaling, &timer );

    Supervisor supervisor( &gui, &radio, &demo, &environment );
    supervisor.Init( );

    system_uart_flush( );

    if( system_gpio_get_pin_state( { BUTTON_BLUE_PORT, BUTTON_BLUE_PIN } ) == SYSTEM_GPIO_PIN_STATE_LOW )
    {
        automatic_mode = true;
    }

    while( 1 )
    {
        signaling.Runtime( );
        if( automatic_mode == true )
        {
            supervisor.RuntimeAuto( );
        }
        else
        {
            supervisor.Runtime( );
        }
    };
}