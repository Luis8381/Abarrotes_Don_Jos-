-- -------------------------------------------------------------
-- TablePlus 26.10.22(802)
--
-- https://tableplus.com/
--
-- Database: login
-- Generation Time: 2026-09-25 17:00:29.4370
-- -------------------------------------------------------------


/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;


DROP TABLE IF EXISTS `productos`;
CREATE TABLE `productos` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `nombre_articulo` varchar(100) NOT NULL,
  `categoria` varchar(50) DEFAULT NULL,
  `existencia` int(11) NOT NULL DEFAULT 0,
  `precio` decimal(10,2) NOT NULL,
  `proveedor_id` int(11) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `proveedor_id` (`proveedor_id`),
  CONSTRAINT `productos_ibfk_1` FOREIGN KEY (`proveedor_id`) REFERENCES `proveedores` (`id`) ON DELETE SET NULL
) ENGINE=InnoDB AUTO_INCREMENT=14 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
-- =============================================================
-- Punto de venta - Abarrotera Don José
-- Esquema completo + datos del respaldo del 2026-09-25
-- Para MariaDB (Layerbase). Ejecutar sobre la base "abarrotesdb".
--
-- OJO: borra y vuelve a crear las tablas. Si ya había datos
-- en abarrotesdb, se pierden.
-- =============================================================

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

DROP TABLE IF EXISTS detalle_venta;
DROP TABLE IF EXISTS ventas;
DROP TABLE IF EXISTS clientes;
DROP TABLE IF EXISTS productos;
DROP TABLE IF EXISTS proveedores;
DROP TABLE IF EXISTS usuarios;

-- ------------------------------------------------------------
-- Usuarios
-- password mide 255 porque el hash SHA-256 ocupa 64 caracteres
-- ------------------------------------------------------------
CREATE TABLE usuarios (
  id INT NOT NULL AUTO_INCREMENT,
  usuario VARCHAR(50) NOT NULL,
  password VARCHAR(255) NOT NULL,
  salt VARCHAR(64) DEFAULT NULL,
  email VARCHAR(100) DEFAULT NULL,
  rol ENUM('administrador','vendedor') NOT NULL DEFAULT 'vendedor',
  activo TINYINT(1) NOT NULL DEFAULT 1,
  PRIMARY KEY (id),
  UNIQUE KEY usuario (usuario)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- ------------------------------------------------------------
-- Proveedores
-- ------------------------------------------------------------
CREATE TABLE proveedores (
  id INT NOT NULL AUTO_INCREMENT,
  nombre_empresa VARCHAR(100) NOT NULL,
  telefono VARCHAR(20) DEFAULT NULL,
  PRIMARY KEY (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- ------------------------------------------------------------
-- Productos (imagen guarda solo el nombre del archivo)
-- ------------------------------------------------------------
CREATE TABLE productos (
  id INT NOT NULL AUTO_INCREMENT,
  nombre_articulo VARCHAR(100) NOT NULL,
  categoria VARCHAR(50) DEFAULT NULL,
  existencia INT NOT NULL DEFAULT 0,
  precio DECIMAL(10,2) NOT NULL,
  proveedor_id INT DEFAULT NULL,
  imagen VARCHAR(255) DEFAULT NULL,
  PRIMARY KEY (id),
  KEY proveedor_id (proveedor_id),
  CONSTRAINT productos_ibfk_1 FOREIGN KEY (proveedor_id) REFERENCES proveedores (id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- ------------------------------------------------------------
-- Clientes
-- ------------------------------------------------------------
CREATE TABLE clientes (
  id INT NOT NULL AUTO_INCREMENT,
  nombre VARCHAR(100) NOT NULL,
  telefono VARCHAR(20) DEFAULT NULL,
  email VARCHAR(100) DEFAULT NULL,
  direccion VARCHAR(200) DEFAULT NULL,
  fecha_registro DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- ------------------------------------------------------------
-- Ventas
-- Si se borra un cliente, sus ventas quedan como "Público general".
-- Un usuario con ventas no se puede borrar (se desactiva).
-- ------------------------------------------------------------
CREATE TABLE ventas (
  id INT NOT NULL AUTO_INCREMENT,
  cliente_id INT DEFAULT NULL,
  usuario_id INT NOT NULL,
  metodo_pago VARCHAR(20) NOT NULL,
  total DECIMAL(10,2) NOT NULL,
  fecha DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY cliente_id (cliente_id),
  KEY usuario_id (usuario_id),
  CONSTRAINT ventas_fk_cliente FOREIGN KEY (cliente_id) REFERENCES clientes (id) ON DELETE SET NULL,
  CONSTRAINT ventas_fk_usuario FOREIGN KEY (usuario_id) REFERENCES usuarios (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- ------------------------------------------------------------
-- Detalle de cada venta (guarda el nombre del artículo por si
-- después se borra el producto)
-- ------------------------------------------------------------
CREATE TABLE detalle_venta (
  id INT NOT NULL AUTO_INCREMENT,
  venta_id INT NOT NULL,
  producto_id INT DEFAULT NULL,
  nombre_articulo VARCHAR(100) NOT NULL,
  cantidad INT NOT NULL,
  precio_unitario DECIMAL(10,2) NOT NULL,
  subtotal DECIMAL(10,2) NOT NULL,
  PRIMARY KEY (id),
  KEY venta_id (venta_id),
  KEY producto_id (producto_id),
  CONSTRAINT detalle_fk_venta FOREIGN KEY (venta_id) REFERENCES ventas (id) ON DELETE CASCADE,
  CONSTRAINT detalle_fk_producto FOREIGN KEY (producto_id) REFERENCES productos (id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- ------------------------------------------------------------
-- Datos
-- ------------------------------------------------------------
INSERT INTO proveedores (id, nombre_empresa, telefono) VALUES
(1, 'Productos', '8112345678'),
(2, 'Distribuidora Limpiomax', '5598765432');

INSERT INTO productos (id, nombre_articulo, categoria, existencia, precio, proveedor_id) VALUES
(1, 'Coca-Cola', 'Bebidas', 40, 25.00, 1),
(2, 'Pepsi', 'Bebidas', 40, 22.00, 1),
(3, 'Bimbo Blanco G', 'Pan', 20, 50.00, 1),
(5, 'Picafresas', 'Dulces', 100, 2.00, 1),
(6, 'Sabritas Original 57g', 'Snacks', 30, 25.00, 1),
(7, 'Sabritas Adobadas 255g', 'Snacks', 20, 64.50, 1),
(8, 'Ruffles Original 185g', 'Snacks', 20, 52.99, 1),
(9, 'Sabritas Limón 160g', 'Snacks', 20, 53.00, 1),
(10, 'Ruffles Queso 280g', 'Snacks', 20, 64.50, 1),
(11, 'Clorox Original 1.89L', 'Desinfectantes', 15, 25.00, 2),
(12, 'Pinol Lavanda 828ml', 'Desinfectantes', 20, 25.00, 2),
(13, 'Botella de Agua 600ml', 'Bebidas', 20, 15.00, 1);

-- El administrador entra con admin / 1234.
-- La contraseña va en texto plano con salt vacío A PROPÓSITO: el programa
-- la convierte a hash la próxima vez que se abre. Después, cámbiala
-- desde la pantalla "Usuarios".
INSERT INTO usuarios (id, usuario, password, salt, email, rol, activo) VALUES
(1, 'admin', '1234', '', 'admin@correo.com', 'administrador', 1);

SET FOREIGN_KEY_CHECKS = 1;