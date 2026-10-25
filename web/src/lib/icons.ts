// Phosphor icons, imported one by one so only the glyphs in use reach the hub's flash.
// Add a name here before using it in <Icon>.
import fan from '@phosphor-icons/core/assets/regular/fan.svg?raw';
import plus from '@phosphor-icons/core/assets/regular/plus.svg?raw';
import power from '@phosphor-icons/core/assets/regular/power.svg?raw';
import caretLeft from '@phosphor-icons/core/assets/regular/caret-left.svg?raw';
import caretRight from '@phosphor-icons/core/assets/regular/caret-right.svg?raw';
import x from '@phosphor-icons/core/assets/regular/x.svg?raw';
import sunDim from '@phosphor-icons/core/assets/regular/sun-dim.svg?raw';
import crosshair from '@phosphor-icons/core/assets/regular/crosshair.svg?raw';
import lightbulb from '@phosphor-icons/core/assets/regular/lightbulb.svg?raw';
import arrowsCounterClockwise from '@phosphor-icons/core/assets/regular/arrows-counter-clockwise.svg?raw';
import timer from '@phosphor-icons/core/assets/regular/timer.svg?raw';
import broadcast from '@phosphor-icons/core/assets/regular/broadcast.svg?raw';
import checkCircle from '@phosphor-icons/core/assets/regular/check-circle.svg?raw';
import circle from '@phosphor-icons/core/assets/regular/circle.svg?raw';
import minusCircle from '@phosphor-icons/core/assets/regular/minus-circle.svg?raw';
import numberOne from '@phosphor-icons/core/assets/regular/number-one.svg?raw';
import numberTwo from '@phosphor-icons/core/assets/regular/number-two.svg?raw';
import numberThree from '@phosphor-icons/core/assets/regular/number-three.svg?raw';
import numberFour from '@phosphor-icons/core/assets/regular/number-four.svg?raw';
import numberFive from '@phosphor-icons/core/assets/regular/number-five.svg?raw';
import numberSix from '@phosphor-icons/core/assets/regular/number-six.svg?raw';
import squaresFour from '@phosphor-icons/core/assets/regular/squares-four.svg?raw';
import waveSine from '@phosphor-icons/core/assets/regular/wave-sine.svg?raw';
import gearSix from '@phosphor-icons/core/assets/regular/gear-six.svg?raw';
import dotsThree from '@phosphor-icons/core/assets/regular/dots-three.svg?raw';
import dotsSixVertical from '@phosphor-icons/core/assets/regular/dots-six-vertical.svg?raw';
import gauge from '@phosphor-icons/core/assets/regular/gauge.svg?raw';
import wifiHigh from '@phosphor-icons/core/assets/regular/wifi-high.svg?raw';
import wifiMedium from '@phosphor-icons/core/assets/regular/wifi-medium.svg?raw';
import wifiLow from '@phosphor-icons/core/assets/regular/wifi-low.svg?raw';
import lockSimple from '@phosphor-icons/core/assets/regular/lock-simple.svg?raw';
import arrowCircleUp from '@phosphor-icons/core/assets/regular/arrow-circle-up.svg?raw';
import downloadSimple from '@phosphor-icons/core/assets/regular/download-simple.svg?raw';
import uploadSimple from '@phosphor-icons/core/assets/regular/upload-simple.svg?raw';
import shieldCheck from '@phosphor-icons/core/assets/regular/shield-check.svg?raw';
import play from '@phosphor-icons/core/assets/regular/play.svg?raw';
import magnifyingGlass from '@phosphor-icons/core/assets/regular/magnifying-glass.svg?raw';
import trash from '@phosphor-icons/core/assets/regular/trash.svg?raw';
import fanFill from '@phosphor-icons/core/assets/fill/fan-fill.svg?raw';
import squaresFourFill from '@phosphor-icons/core/assets/fill/squares-four-fill.svg?raw';
import waveSineFill from '@phosphor-icons/core/assets/fill/wave-sine-fill.svg?raw';
import gearSixFill from '@phosphor-icons/core/assets/fill/gear-six-fill.svg?raw';
import checkCircleFill from '@phosphor-icons/core/assets/fill/check-circle-fill.svg?raw';
import recordFill from '@phosphor-icons/core/assets/fill/record-fill.svg?raw';

export const icons = {
  'fan': fan,
  'plus': plus,
  'power': power,
  'caret-left': caretLeft,
  'caret-right': caretRight,
  'x': x,
  'sun-dim': sunDim,
  'crosshair': crosshair,
  'lightbulb': lightbulb,
  'arrows-counter-clockwise': arrowsCounterClockwise,
  'timer': timer,
  'broadcast': broadcast,
  'check-circle': checkCircle,
  'circle': circle,
  'minus-circle': minusCircle,
  'number-one': numberOne,
  'number-two': numberTwo,
  'number-three': numberThree,
  'number-four': numberFour,
  'number-five': numberFive,
  'number-six': numberSix,
  'squares-four': squaresFour,
  'wave-sine': waveSine,
  'gear-six': gearSix,
  'dots-three': dotsThree,
  'dots-six-vertical': dotsSixVertical,
  'gauge': gauge,
  'wifi-high': wifiHigh,
  'wifi-medium': wifiMedium,
  'wifi-low': wifiLow,
  'lock-simple': lockSimple,
  'arrow-circle-up': arrowCircleUp,
  'download-simple': downloadSimple,
  'upload-simple': uploadSimple,
  'shield-check': shieldCheck,
  'play': play,
  'magnifying-glass': magnifyingGlass,
  'trash': trash,
  'fan-fill': fanFill,
  'squares-four-fill': squaresFourFill,
  'wave-sine-fill': waveSineFill,
  'gear-six-fill': gearSixFill,
  'check-circle-fill': checkCircleFill,
  'record-fill': recordFill,
} as const;

export type IconName = keyof typeof icons;
