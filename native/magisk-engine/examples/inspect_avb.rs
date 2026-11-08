use avbroot::format::avb::{self, Descriptor};
fn main() {
    let path = std::env::args().nth(1).unwrap();
    let b = std::fs::read(path).unwrap();
    let (h, f, _) = avb::load_image(std::io::Cursor::new(b)).unwrap();
    println!(
        "algorithm {:?}, descriptors {}, footer {:?}",
        h.algorithm_type,
        h.descriptors.len(),
        f
    );
    for d in &h.descriptors {
        println!(
            "{}",
            match d {
                Descriptor::Hash(d) => format!(
                    "Hash {} {} size {}",
                    d.partition_name, d.hash_algorithm, d.image_size
                ),
                Descriptor::HashTree(d) => format!("HashTree {}", d.partition_name),
                _ => "other".into(),
            }
        );
    }
}
