// Reference implementation used only by optional differential tests.
// Reference implementation used only by optional differential tests.
use aptx_adaptive::{Config, DecoderLevel, Encoder, Profile, WINDOW_SLACK};
use std::{env, fs};

fn main() {
    let a: Vec<String> = env::args().collect();
    let rate: u32 = a[1].parse().unwrap();
    let bitrate: u32 = a[2].parse().unwrap();
    let packet: usize = a[3].parse().unwrap();
    let ms: usize = a[4].parse().unwrap();
    let chunk: usize = a[5].parse().unwrap();
    let ll = a[6] == "ll";
    let config = Config { sample_rate: rate, channels: 2,
        profile: if ll { Profile::Ll } else { Profile::Hq },
        level: if a[7] == "r1" { DecoderLevel::R1 } else { DecoderLevel::R2 } };
    let pcm: Vec<i32> = fs::read(&a[8]).unwrap().chunks_exact(4)
        .map(|x| i32::from_le_bytes(x.try_into().unwrap())).collect();
    let mut encoder = Encoder::new(config, bitrate).unwrap();
    let mut window = vec![0; packet + WINDOW_SLACK];
    let mut queue = Vec::new();
    let mut output = Vec::new();
    let (mut consumed, mut deficit) = (0i64, 0i64);
    let period = (rate as usize * 2 * ms / 1000) as i64;
    for data in pcm.chunks(chunk * 2) {
        queue.extend_from_slice(data);
        let p = encoder.encode_packet(&queue, &mut window).unwrap();
        queue.drain(..p.consumed);
        consumed += p.consumed as i64;
        let n = match p.written {
            0 if consumed < period + deficit => continue,
            0 => encoder.flush(&mut window).unwrap(),
            n => n,
        };
        if n == 0 { continue; }
        output.extend_from_slice(&window[..n]);
        deficit += period - consumed;
        consumed = 0;
        if ll { deficit = 0; }
        encoder.set_bitrate(if deficit > 96 { bitrate - bitrate / 4 } else { bitrate });
    }
    fs::write(&a[9], output).unwrap();
}
