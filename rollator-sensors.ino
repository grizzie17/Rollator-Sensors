
#define _DEBUG
#include <YogiDebug.h>

#include <ADXL345_setup.h>  // includes SparkFun ADXL345 Library
#include <WatchDog.h>
// #include <Wire.h>

#include <YogiDelay.h>
#include <YogiPitches.h>
#include <YogiSleep.h>
#include <YogiSonic.h>


#ifndef __asm__
#    ifdef _MSC_VER
#        define __asm__ __asm
#    endif
#endif


const uint8_t k_pinINT = 2;  // INT0
const uint8_t k_pinRelay = 3;


const uint8_t kPinVibeRight = 11;  // PWM
const uint8_t kPinVibeLeft = 6;    // PWM

const uint8_t kPinSonicEchoRight = 12;
const uint8_t kPinSonicTrigRight = 12;

const uint8_t kPinSonicEchoFrontRight = 10;
const uint8_t kPinSonicTrigFrontRight = 10;

const uint8_t kPinSonicEchoFrontLeft = 7;
const uint8_t kPinSonicTrigFrontLeft = 7;

const uint8_t kPinSonicEchoLeft = 5;
const uint8_t kPinSonicTrigLeft = 5;


const uint8_t kPinPotDist = A0;
const uint8_t kPinSDA = A4;
const uint8_t kPinSCL = A5;


const unsigned int k_minDistance = 10;  // cm


typedef enum orientation_t
        : uint8_t
{
    OR_UNKNOWN,
    OR_VERTICAL,
    OR_HORIZONTAL
} orientation_t;

orientation_t g_eOrientation = OR_UNKNOWN;

bool          g_bSleepy = false;
bool          g_bActiveLaydown = false;
unsigned long g_uLaying = 0;


unsigned long g_uTimeCurrent = 0;
unsigned long g_uTimePrevious = 0;
unsigned long g_uTimeInterrupt = 0;

const uint8_t       k_uAdxlDelaySleep = 45;
const unsigned long k_uDelaySleep = 600 * k_uAdxlDelaySleep;


YogiDelay g_tSonicDelay;
int       g_nSonicCycle = 0;
long      g_nSonicCount = 0;


YogiSleep g_tSleep;


//============== RELAY ===============

inline void
relaySetup()
{
    pinMode( k_pinRelay, OUTPUT );
}


void
relayEnable()
{
    relaySetup();
    digitalWrite( k_pinRelay, HIGH );
    delay( 400 );
}


void
relayDisable()
{
    relaySetup();
    digitalWrite( k_pinRelay, LOW );
}


//================ ADXL =================

void
adxlAttachInterrupt()
{
    pinMode( k_pinINT, INPUT );
    attachInterrupt(
            digitalPinToInterrupt( k_pinINT ), adxlIntHandler, RISING );
}

void
adxlDetachInterrupt()
{
    detachInterrupt( digitalPinToInterrupt( k_pinINT ) );
}


void
adxlDrowsy()
{
    g_uCountInterrupt = 0;
    adxl.setInterruptMask( 0 );
    adxl.getInterruptSource();  // clear mInterrupts

    adxl.setInterruptMask( k_maskActivity );
    adxl.setLowPower( true );
}


void
adxlSleep()
{
    adxlDetachInterrupt();
    g_uCountInterrupt = 0;
    adxl.getInterruptSource();  // clear mInterrupts

    adxl.setInterruptMask( 0 );
    adxl.setLowPower( true );
}


void
adxlWakeup()
{
    adxl.setLowPower( false );
    adxl.setInterruptMask( k_maskAll );
    g_uCountInterrupt = 0;
    adxl.getInterruptSource();
    adxlAttachInterrupt();
    adxlIntHandler();
    g_nActivity = 0;
}


//================= WATCHDOG =================

volatile bool g_bWatchDogInterrupt = false;

void
watchdogIntHandler()
{
    g_bWatchDogInterrupt = true;
}


void
watchdogSleep()
{
    DEBUG_PRINTLN( "Watchdog Sleep" );
    DEBUG_DELAY( 300 );
    relayDisable();
    adxlSleep();
    WatchDog::setPeriod( OVF_4000MS );
    WatchDog::start();
    g_tSleep.powerDown();
}


void
watchdogWakeup()
{
    WatchDog::stop();
    adxlWakeup();
    relayEnable();
    g_uTimeInterrupt = millis();
    DEBUG_PRINTLN( "Watchdog Wakeup" );
}


void
watchdogHandler()
{
    if ( isHorizontal() )
    {
        // go back to sleep
        g_eOrientation = OR_HORIZONTAL;
        adxlSleep();
        WatchDog::start();
        g_tSleep.sleep();
    }
    else
    {
        // cancel watchdog
        watchdogWakeup();

        g_uLaying = 0;
        g_bActiveLaydown = false;
        g_eOrientation = OR_VERTICAL;
    }
}


//============== LED ===============

#if false
class LedControl
{
public:
    LedControl( uint8_t pin )
            : m_pin( pin )
            , m_val( 0 )
            , m_on( false )
    {}

    void
    init()
    {
        pinMode( m_pin, OUTPUT );
        m_val = 0;
    }

    void
    on( uint8_t value = HIGH )
    {
        if ( m_val != value )
        {
            m_val = value;
            analogWrite( m_pin, m_val );
            m_on = true;
        }
    }

    void
    off()
    {
        if ( 0 < m_val )
        {
            m_val = 0;
            analogWrite( m_pin, m_val );
            m_on = false;
        }
    }

protected:
    bool    m_on;
    uint8_t m_val;
    uint8_t m_pin;
};

#endif  // false


//=========== VIBE ===========

typedef struct VibeItem
{
    int  nDuration;
    bool bOn;
} VibeItem;

// following arrays contain millisecond times for on..off
VibeItem g_aVibeFront[] = { { 1000, true }, { 1000, false } };
VibeItem g_aVibeSide[] = { { 200, true }, { 800, false } };
VibeItem g_aVibeBoth[] = { { 1000, true }, { 800, false }, { 200, true },
    { 800, false }, { 200, true }, { 800, false } };

int g_nVibeCountFront = sizeof( g_aVibeFront ) / sizeof( g_aVibeFront[0] );
int g_nVibeCountSide = sizeof( g_aVibeSide ) / sizeof( g_aVibeSide[0] );
int g_nVibeCountBoth = sizeof( g_aVibeBoth ) / sizeof( g_aVibeBoth[0] );


class VibeControl
{
public:
    VibeControl( uint8_t pin )
            : m_pin( pin )
            , m_val( 0 )
            , m_buzzing( false )
            , m_tDelay()
    {}

public:
    void
    init()
    {
        pinMode( m_pin, OUTPUT );
        m_val = 0;
        m_buzzing = false;
        m_tDelay.init( 1000 );
    }


    void
    reset()
    {
        m_tDelay.reset();
    }

    void
    setPattern( int nVibeCount, VibeItem* pVibeList )
    {
        if ( pVibeList != m_aVibeList )
        {
            m_aVibeList = pVibeList;
            m_nCountVibe = nVibeCount;
            m_index = 0;
        }
    }

    void
    on( uint8_t value )
    {
        if ( m_val != value )
        {
            m_val = value;
        }
    }

    void
    cycle( unsigned long nTimeCurrent = 0 )
    {
        if ( 0 < m_val )
        {
            if ( m_tDelay.timesUp( nTimeCurrent ) )
            {
                if ( m_aVibeList[m_index].bOn )
                {
                    m_buzzing = true;
                    analogWrite( m_pin, m_val );
                }
                else
                {
                    m_buzzing = false;
                    analogWrite( m_pin, 0 );
                }
                m_tDelay.newDelay( m_aVibeList[m_index].nDuration );
                ++m_index;
                if ( m_nCountVibe <= m_index )
                    m_index = 0;
            }
        }
    }

    void
    off()
    {
        if ( 0 < m_val )
        {
            m_val = 0;
            m_index = 0;
            m_buzzing = false;
            m_aVibeList = nullptr;
            analogWrite( m_pin, m_val );
        }
    }

    bool
    isBuzzing()
    {
        return m_buzzing;
    }

    void
    sync( VibeControl& other )
    {
        if ( m_aVibeList == other.m_aVibeList )
        {
            if ( m_index != other.m_index )
            {
                m_index = other.m_index;
                int nDelay = other.m_tDelay.getDelay();
                m_tDelay.newDelay( nDelay );
                other.m_tDelay.newDelay( nDelay );
            }
        }
    }

protected:
    uint8_t   m_pin;
    uint8_t   m_val;
    bool      m_buzzing = false;
    YogiDelay m_tDelay;
    int       m_nCountVibe = 0;
    VibeItem* m_aVibeList = nullptr;
    int       m_index = 0;
};


VibeControl g_tVibeLeft( kPinVibeLeft );
VibeControl g_tVibeRight( kPinVibeRight );

//============== SLEEP ===============

void
enterSleep()
{
    DEBUG_PRINTLN( "Sleepy" );
    DEBUG_DELAY( 300 );

    g_tVibeLeft.off();
    g_tVibeRight.off();

    relayDisable();  // power-down sensors

    // don't generate inactivity interrupts during sleep
    adxlDrowsy();

    g_bSleepy = true;
    g_tSleep.prepareSleep();
    adxlAttachInterrupt();
    g_tSleep.sleep();
    g_tSleep.postSleep();

    // we wakeup here
    g_bSleepy = false;
    relayEnable();
    adxlWakeup();
    DEBUG_PRINTLN( "Wake Up" );

    // ? do we need to reintialize any of the sensors ?
}


//=============== SONIC ==================

class CAvgSonic
{
public:
    CAvgSonic( YogiSonic& sonic, char* sName )
            : m_rSonic( sonic )
            , m_tDelay( 200 )
            , m_nDistCurrent( 0 )
            , m_nDistPrevious( 0 )
            , m_nIndex( 0 )
    {
        reset();
        strcpy( m_sName, sName );
    }
    ~CAvgSonic()
    {}

public:
    bool
    timesUp( unsigned long uTimeCurrent )
    {
        return m_tDelay.timesUp( uTimeCurrent );
    }


    void
    reset()
    {
        for ( int i = 0; i < k_nSize; ++i )
            m_aDist[i] = 0;
        m_nIndex = 0;
        m_nDistCurrent = 0;
        m_nDistPrevious = 0;
    }


    bool
    isDirty()
    {
        bool bDirty = false;
        long distNew = this->getDistanceCm();
        long distPrev = m_nDistPrevious;
        m_nDistPrevious = m_nDistCurrent;
        m_nDistCurrent = distNew;
        if ( distPrev != distNew )
        {
            bDirty = true;
        }
        return bDirty;
    }


    inline long
    getDistance()
    {
        return m_nDistCurrent;
    }


    long
    getDistanceCm()
    {
        long dist = m_rSonic.getDistanceCm();
        if ( 0 < dist && dist < k_minDistance )
            dist = 0;
        return dist;
    }


protected:
    char             m_sName[24];
    YogiSonic&       m_rSonic;
    YogiDelay        m_tDelay;
    long             m_nDistCurrent;
    long             m_nDistPrevious;
    static const int k_nSize = 3;
    int              m_nIndex;
    long             m_aDist[k_nSize];
};


YogiSonic g_tSonicLeft( kPinSonicTrigLeft, kPinSonicEchoLeft );
YogiSonic g_tSonicFrontLeft( kPinSonicTrigFrontLeft, kPinSonicEchoFrontLeft );
YogiSonic g_tSonicFrontRight(
        kPinSonicTrigFrontRight, kPinSonicEchoFrontRight );
YogiSonic g_tSonicRight( kPinSonicTrigRight, kPinSonicEchoRight );

CAvgSonic g_tAvgLeft( g_tSonicLeft, "Left" );
CAvgSonic g_tAvgFrontLeft( g_tSonicFrontLeft, "FrontLeft" );
CAvgSonic g_tAvgFrontRight( g_tSonicFrontRight, "FrontRight" );
CAvgSonic g_tAvgRight( g_tSonicRight, "Right" );


int g_nPotValueSide = 20;  // distance in cm to alarm
int g_nPotValueFront = g_nPotValueSide * 3 / 2;


void
sonicSetup()
{
    g_tSonicLeft.init();
    g_tSonicFrontLeft.init();
    g_tSonicFrontRight.init();
    g_tSonicRight.init();

#define SONIC_SENSOR_TOTAL 4  // count of ultrasonic sensors
#define SONIC_SENSOR_HZ    4  // Hz rate per sensor
#define SONIC_SENSORS_HZ   ( SONIC_SENSOR_TOTAL * SONIC_SENSOR_HZ )

    g_nSonicCount = 0;
    g_nSonicCycle = 0;
    g_tSonicDelay.init( 1000 / SONIC_SENSORS_HZ );
}


int
potentiometerRead( uint8_t pin, long nRange )
{
    return max( 10, abs( (long)analogRead( pin ) * nRange / 1024 ) );
}

void
updatePotValues()
{
    pinMode( kPinPotDist, INPUT );
    g_nPotValueSide = potentiometerRead( kPinPotDist, 100 );
    g_nPotValueFront = g_nPotValueSide * 3 / 2;

    DEBUG_VPRINT( "Pot Values: Side=", g_nPotValueSide );
    DEBUG_VPRINTLN( "; Front=", g_nPotValueFront );

    g_tSonicLeft.setMaxDistance( g_nPotValueSide );
    g_tSonicFrontLeft.setMaxDistance( g_nPotValueFront );
    g_tSonicFrontRight.setMaxDistance( g_nPotValueFront );
    g_tSonicRight.setMaxDistance( g_nPotValueSide );
}


bool
isHorizontal()
{
    int x, y, z;
    adxl.readAccel( &x, &y, &z );
    return abs( z ) < 100 ? true : false;
}


bool g_bLayingDown = false;

bool
isLayingdown()
{
    if ( isHorizontal() )
    {
        ++g_uLaying;
        return 10 < g_uLaying ? true : false;
    }
    else
    {
        g_uLaying = 0;
        return false;
    }
}


//=============== STATES ==================

void
orientationUnknown()
{
    g_eOrientation = isHorizontal() ? OR_HORIZONTAL : OR_VERTICAL;
}

void
orientationVertical()
{
    if ( isLayingdown() )
    {
        g_eOrientation = OR_HORIZONTAL;
    }
    else
    {
        uint8_t mInterrupts = 0;
        g_uTimeCurrent = millis();
        if ( 0 < g_uCountInterrupt )
        {
            if ( ! g_tVibeLeft.isBuzzing() && ! g_tVibeRight.isBuzzing() )
            {
                mInterrupts = adxlGetInterrupts();
                g_uTimeInterrupt = millis();
            }
            else
            {
                DEBUG_PRINTLN( "Ignoring Interrupt" );
                adxl.getInterruptSource();
                mInterrupts = 0;
            }
        }
        else
        {
            if ( k_uDelaySleep < g_uTimeCurrent - g_uTimeInterrupt )
            {
                adxl.getInterruptSource();
                g_uTimeInterrupt = g_uTimeCurrent;
                mInterrupts = ADXL_M_INACTIVITY;
            }
        }

        if ( 0 != mInterrupts )
        {
            adxlDataHandler( mInterrupts );
            if ( 0 != ( mInterrupts & ADXL_M_INACTIVITY ) )
            {
                WatchDog::stop();
                enterSleep();

                // stuff to do when we wake up
                g_nActivity = 0;
                updatePotValues();
                g_tAvgLeft.reset();
                g_tAvgFrontLeft.reset();
                g_tAvgFrontRight.reset();
                g_tAvgRight.reset();
                g_nSonicCycle = 0;
                g_nSonicCount = 0;
            }
            g_uTimeInterrupt = millis();
        }
        else
        {
            bool bDirty = false;
            if ( g_tSonicDelay.timesUp( g_uTimeCurrent ) )
            {
                switch ( g_nSonicCycle++ )
                {
                case 0:
                    bDirty = g_tAvgLeft.isDirty();
                    break;
                case 1:
                    bDirty = g_tAvgRight.isDirty();
                    break;
                case 2:
                    bDirty = g_tAvgFrontLeft.isDirty();
                    break;
                case 3:
                    bDirty = g_tAvgFrontRight.isDirty();
                    break;
                default:
                    g_nSonicCycle = 0;
                    break;
                }
            }

            if ( bDirty )
            {
                long nL = g_tAvgLeft.getDistance();
                long nFL = g_tAvgFrontLeft.getDistance();
                long nFR = g_tAvgFrontRight.getDistance();
                long nR = g_tAvgRight.getDistance();

                DEBUG_STATEMENT( ++g_nSonicCount );
                DEBUG_VPRINT( "L = ", nL );
                DEBUG_VPRINT( ";  FL = ", nFL );
                DEBUG_VPRINT( ";  FR = ", nFR );
                DEBUG_VPRINT( ";  R = ", nR );
                DEBUG_VPRINT( "; pS = ", g_nPotValueSide );
                DEBUG_VPRINT( "; pF = ", g_nPotValueFront );
                DEBUG_VPRINTLN( "; #", g_nSonicCount );

                // both sides are within range
                // probably going through door
                if ( 0 < nL && 0 < nR )
                {
                    nL = 0;
                    nFL = 0;
                    nFR = 0;
                    nR = 0;
                }

                bool    bVibeLeft = false;
                bool    bVibeRight = false;
                uint8_t nVibeValueLeft = 0;
                uint8_t nVibeValueRight = 0;

                if ( 1 < nL || 1 < nFL )
                {
                    bVibeLeft = true;
                    if ( 1 < nFL )
                        nVibeValueLeft = map(
                                nFL, g_nPotValueFront, k_minDistance, 1, 255 );
                    else
                        nVibeValueLeft = map(
                                nL, g_nPotValueSide, k_minDistance, 1, 255 );

                    if ( 1 < nFL && 1 < nL )
                    {
                        g_tVibeLeft.setPattern( g_nVibeCountBoth, g_aVibeBoth );
                    }
                    else if ( 1 < nFL )
                    {
                        g_tVibeLeft.setPattern(
                                g_nVibeCountFront, g_aVibeFront );
                    }
                    else
                    {
                        g_tVibeLeft.setPattern( g_nVibeCountSide, g_aVibeSide );
                    }
                }


                if ( 1 < nFR || 1 < nR )
                {
                    bVibeRight = true;
                    if ( 1 < nFR )
                        nVibeValueRight = map(
                                nFR, g_nPotValueFront, k_minDistance, 1, 255 );
                    else
                        nVibeValueRight = map(
                                nR, g_nPotValueSide, k_minDistance, 1, 255 );

                    if ( 1 < nFR && 1 < nR )
                    {
                        g_tVibeRight.setPattern(
                                g_nVibeCountBoth, g_aVibeBoth );
                    }
                    else if ( 1 < nFR )
                    {
                        g_tVibeRight.setPattern(
                                g_nVibeCountFront, g_aVibeFront );
                    }
                    else
                    {
                        g_tVibeRight.setPattern(
                                g_nVibeCountSide, g_aVibeSide );
                    }
                }


                if ( bVibeLeft )
                    g_tVibeLeft.on( nVibeValueLeft );
                else
                    g_tVibeLeft.off();


                if ( bVibeRight )
                    g_tVibeRight.on( nVibeValueRight );
                else
                    g_tVibeRight.off();


                g_tVibeLeft.sync( g_tVibeRight );
                g_tVibeLeft.cycle( g_uTimeCurrent );
                g_tVibeRight.cycle( g_uTimeCurrent );
            }
            else
            {
                g_tVibeLeft.cycle( g_uTimeCurrent );
                g_tVibeRight.cycle( g_uTimeCurrent );
            }
        }
    }
}


void
orientationHorizontal()
{
    // Check if we are laying-down. If we are not
    // laying-down then switch modes to OR_VERTICAL.
    if ( isLayingdown() )
    {
        if ( ! g_bActiveLaydown )
        {
            g_bActiveLaydown = true;

            watchdogSleep();
        }
    }
    else
    {
        if ( g_bActiveLaydown )
        {
            g_bActiveLaydown = false;
            watchdogWakeup();
            g_eOrientation = OR_VERTICAL;
        }
    }
}


//================== SETUP =====================
void
setup()
{
    DEBUG_OPEN();

    relayEnable();  // power up sensors

    updatePotValues();

    adxlSetup( k_uAdxlDelaySleep );
    adxlAttachInterrupt();

    sonicSetup();

    g_tVibeLeft.init();
    g_tVibeRight.init();


    g_bWatchDogInterrupt = false;
    WatchDog::init( watchdogIntHandler, OVF_4000MS );
    WatchDog::stop();

    g_bLayingDown = false;
    g_uLaying = 0;
    g_eOrientation = isHorizontal() ? OR_HORIZONTAL : OR_VERTICAL;

    g_uTimeInterrupt = millis();
    g_uTimeCurrent = millis();
    g_uTimePrevious = 0;
}


//================= LOOP =======================
void
loop()
{
    if ( g_bWatchDogInterrupt )
    {
        g_bWatchDogInterrupt = false;
        watchdogHandler();
    }
    else
    {
        switch ( g_eOrientation )
        {
        case OR_VERTICAL:
            orientationVertical();
            break;
        case OR_HORIZONTAL:
            orientationHorizontal();
            break;
        case OR_UNKNOWN:
        default:
            orientationUnknown();
            break;
        }
    }
}
